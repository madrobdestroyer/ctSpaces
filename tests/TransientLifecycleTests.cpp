// Production launch/exit/cleanup routines with inert desktop/process boundaries.
#include <windows.h>
#include "RetainedFixture.h"
#include <shellapi.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "../ProgressUI.h"
static unsigned presentation=0,launchRequests=0,blockedCreateRequests=0,preparedProcessRequests=0,paintRequests=0,checks=0,failures=0,notices=0;
static bool unknownProcesses=false,swapDuringProbe=false,collisionDuringCreate=false,swapDuringProcessCreate=false,swapAttempted=false,swapSucceeded=false,collisionHit=false,processCreateHit=false;
static bool swapAfterDirectoryCreation=false,reservationSwapHit=false;
static std::filesystem::path target,foreignRoot,preservedRoot;
static BOOL WINAPI NoShow(HWND,int){++presentation;return FALSE;}
static HWND WINAPI NoFocus(HWND){++presentation;return nullptr;}
static BOOL WINAPI NoForeground(HWND){++presentation;return FALSE;}
static HWND WINAPI NoActive(HWND){++presentation;return nullptr;}
static BOOL WINAPI NoRaise(HWND){++presentation;return FALSE;}
static BOOL WINAPI NoPosition(HWND,HWND,int,int,int,int,UINT){++presentation;return FALSE;}
static BOOL WINAPI NoPaint(HWND,const RECT*,BOOL){++paintRequests;return FALSE;}
static BOOL WINAPI NoRedraw(HWND,const RECT*,HRGN,UINT){++paintRequests;return FALSE;}
static HINSTANCE WINAPI NoShell(HWND,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,INT){++launchRequests;return reinterpret_cast<HINSTANCE>(ERROR_ACCESS_DENIED);}
static BOOL WINAPI NoShellEx(SHELLEXECUTEINFOW*){++launchRequests;return FALSE;}
static BOOL WINAPI NoCreateProcess(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION){
 ++launchRequests;++blockedCreateRequests;
 if(std::filesystem::is_regular_file(target/L"user.js")||
    (std::filesystem::is_regular_file(target/L"ctSpaces")&&std::filesystem::is_directory(target/L"Default")))++preparedProcessRequests;
 if(swapDuringProcessCreate){swapDuringProcessCreate=false;processCreateHit=true;swapAttempted=true;
  std::error_code error;std::filesystem::rename(target,preservedRoot,error);
  if(!error)std::filesystem::rename(foreignRoot,target,error);
  swapSucceeded=!error;
 }
 // This boundary never calls the native process API, even for the private
 // firefox.exe sentinel. A recorded request is not an actual browser launch.
 SetLastError(ERROR_ACCESS_DENIED);return FALSE;
}
static void NoProgressShow(HWND,const std::wstring&,int){}
static void NoProgressHide(HWND){}
static void NoProgressUpdate(HWND,unsigned int){}
static HANDLE WINAPI FixtureSnapshot(DWORD,DWORD){if(unknownProcesses){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}return CreateEventW(nullptr,FALSE,FALSE,nullptr);}
static BOOL WINAPI FixtureFirst(HANDLE,LPPROCESSENTRY32W item){wcscpy_s(item->szExeFile,L"owned-fixture.exe");item->th32ProcessID=GetCurrentProcessId();return TRUE;}
static BOOL WINAPI FixtureNext(HANDLE,LPPROCESSENTRY32W){
 if(swapDuringProbe){swapDuringProbe=false;swapAttempted=true;std::error_code error;std::filesystem::rename(target,preservedRoot,error);if(!error)std::filesystem::rename(foreignRoot,target,error);swapSucceeded=!error;}
 SetLastError(ERROR_NO_MORE_FILES);return FALSE;
}
static BOOL WINAPI FixtureCreateDirectory(LPCWSTR path,LPSECURITY_ATTRIBUTES security){
 if(collisionDuringCreate&&std::filesystem::path(path)==target){collisionDuringCreate=false;collisionHit=true;
  if(!::CreateDirectoryW(path,security))throw std::runtime_error("Private collision directory could not be created");
  std::ofstream stream(target/L"foreign.txt",std::ios::binary);stream<<"INDEPENDENT FOLDER";stream.close();
  SetLastError(ERROR_ALREADY_EXISTS);return FALSE;
 }
 const BOOL created=::CreateDirectoryW(path,security);
 if(created&&swapAfterDirectoryCreation&&std::filesystem::path(path)==target){
  swapAfterDirectoryCreation=false;reservationSwapHit=true;swapAttempted=true;
  std::error_code error;std::filesystem::rename(target,preservedRoot,error);
  if(!error)std::filesystem::rename(foreignRoot,target,error);
  swapSucceeded=!error;
 }
 return created;
}
#define ShowWindow NoShow
#define SetFocus NoFocus
#define SetForegroundWindow NoForeground
#define SetActiveWindow NoActive
#define BringWindowToTop NoRaise
#define SetWindowPos NoPosition
#define InvalidateRect NoPaint
#define RedrawWindow NoRedraw
#define ShellExecuteW NoShell
#define ShellExecuteExW NoShellEx
#define CreateProcessW NoCreateProcess
#define CreateDirectoryW FixtureCreateDirectory
#define CreateToolhelp32Snapshot FixtureSnapshot
#define Process32FirstW FixtureFirst
#define Process32NextW FixtureNext
#define ProgressUI_PostShow NoProgressShow
#define ProgressUI_PostHide NoProgressHide
#define ProgressUI_PostProgress NoProgressUpdate
#define CTSPACES_TRANSIENT_RESERVATION_TEST_HOOKS
#define CTSPACES_INSTALLER_TEST_HOOKS
#define wWinMain ProductionEntryNotRun
#ifdef CTSPACES_TRANSIENT_BEFORE
#include "../build/app-audit-round8-2026-10-15/root/ctSpaces-before.cpp"
#elif defined(CTSPACES_TRANSIENT_EXTENSION_CURRENT_BEFORE)
#include "../build/app-audit-round8-2026-10-15/transient-extension/ctSpaces-current-before.cpp"
#elif defined(CTSPACES_TRANSIENT_EXTENSION_AFTER)
#include "../build/app-audit-round8-2026-10-15/transient-extension/ctSpaces-after.cpp"
#elif defined(CTSPACES_TRANSIENT_EXTENSION_FIXED)
#include "../build/app-audit-round8-2026-10-15/transient-extension/fixed/ctSpaces-after.cpp"
#else
#include "../ctSpaces.cpp"
#endif
#undef wWinMain
#include "DirectoryJunctionFixture.h"
namespace transient_profile {
void BeforeReservation(const std::filesystem::path &path) {
 if(collisionDuringCreate&&path==target){collisionDuringCreate=false;collisionHit=true;
  if(!::CreateDirectoryW(path.c_str(),nullptr))throw std::runtime_error("Private atomic reservation collision fixture failed");
  std::ofstream stream(path/L"foreign.txt",std::ios::binary);stream<<"INDEPENDENT FOLDER";
 }
}
void AfterReservation(const std::filesystem::path &path,HANDLE) {
 if(swapAfterDirectoryCreation&&path==target){swapAfterDirectoryCreation=false;reservationSwapHit=true;swapAttempted=true;
  std::error_code error;std::filesystem::rename(target,preservedRoot,error);
  if(!error)std::filesystem::rename(foreignRoot,target,error);swapSucceeded=!error;
 }
}
}
int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR message,LPCWSTR title,UINT){++notices;std::wcout<<L"INTERCEPTED_NOTICE title="<<(title?title:L"")<<L" message="<<(message?message:L"")<<L'\n';return IDNO;}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path&,std::wstring&){return false;}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path&){}
static void Check(bool ok,const char*message){++checks;if(!ok)++failures;std::cout<<(ok?"PASS ":"FAIL ")<<message<<'\n';}
static void Put(const fs::path&path,const char*value){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out<<value;if(!out)throw std::runtime_error("Private file write failed");}
static std::string Read(const fs::path&path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
static fs::path fixture;
static unsigned caseNumber=0;
static void Seed(const wchar_t*leaf){
 ++caseNumber;g_sDataDir=fixture/std::to_wstring(caseNumber);fs::create_directories(g_sDataDir);
 g_sConfigPath=g_sDataDir/L"config.ini";target=g_sDataDir/leaf;foreignRoot=g_sDataDir/L"Sites/Other";preservedRoot=g_sDataDir/L"original-transient";
 Put(foreignRoot/L"notes.txt","OTHER CLIENT NOTES");Put(foreignRoot/L"Cookies","OTHER CLIENT COOKIES");
 g_activeProfiles.clear();g_sessions.clear();g_hGui=nullptr;g_bUiEnabled=true;g_isLaunchInFlight=false;g_bDefaultProfileUiBusy=false;
 unknownProcesses=swapDuringProbe=collisionDuringCreate=swapDuringProcessCreate=swapAttempted=swapSucceeded=collisionHit=processCreateHit=false;
 swapAfterDirectoryCreation=reservationSwapHit=false;
}
static void AwaitWorkerCompletionBeforeJoin(){
 const ULONGLONG deadline=GetTickCount64()+10000;
 while(g_isLaunchInFlight.load()&&GetTickCount64()<deadline)Sleep(1);
 Check(!g_isLaunchInFlight.load(),"Actual launch worker releases in-flight state before StopLaunchWorker joins or resets it");
 StopLaunchWorker();
}
static void CleanupCases(){
 for(const wchar_t*leaf:{L"Temp",L"Default"}){
  Seed(leaf);Put(target/L"nested/state.txt","DISPOSABLE");std::wstring error;
  Check(RemoveTransientProfileIfIdleAndVerify(target,error)&&!fs::exists(target),"Ordinary nested disposable profile cleanup succeeds");
  Check(RemoveTransientProfileIfIdleAndVerify(target,error),"Conclusive missing transient cleanup is a successful no-op");
  Check(Read(foreignRoot/L"notes.txt")=="OTHER CLIENT NOTES","Ordinary cleanup preserves independent client notes");
  Seed(leaf);Put(target/L"state.txt","LOCKED");
  HANDLE locked=CreateFileW((target/L"state.txt").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
  if(locked==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot lock private transient file");
  Check(!RemoveTransientProfileIfIdleAndVerify(target,error)&&!error.empty()&&Read(target/L"state.txt")=="LOCKED","Locked transient reports failure and preserves its locked bytes");
  CloseHandle(locked);Check(RemoveTransientProfileIfIdleAndVerify(target,error)&&!fs::exists(target),"Cleanup retries successfully after the private lock is released");
  Seed(leaf);Put(target/L"state.txt","UNCERTAIN");unknownProcesses=true;
  Check(!RemoveTransientProfileIfIdleAndVerify(target,error)&&Read(target/L"state.txt")=="UNCERTAIN","Inconclusive process inspection preserves the disposable profile");unknownProcesses=false;
  ActiveProfileInfo active{};active.pid=0x7fffa101;active.profilePath=target;active.type=leaf==std::wstring(L"Temp")?ProfileType::Temporary:ProfileType::Default;
  g_activeProfiles.emplace(ClientBrowserKey{leaf,BrowserKind::Firefox},active);
  Check(!RemoveTransientProfileIfIdleAndVerify(target,error)&&Read(target/L"state.txt")=="UNCERTAIN","Tracked live profile blocks transient deletion before process scanning");g_activeProfiles.clear();
  Seed(leaf);Put(target/L"state.txt","ORIGINAL");swapDuringProbe=true;
  const bool removed=RemoveTransientProfileIfIdleAndVerify(target,error);
  const bool otherLost=Read(foreignRoot/L"notes.txt")!="OTHER CLIENT NOTES"&&Read(target/L"notes.txt")!="OTHER CLIENT NOTES";
  std::cout<<"TRANSIENT_SWAP leaf="<<(leaf[0]=='T'?"Temp":"Default")<<" attempted="<<swapAttempted<<" substituted="<<swapSucceeded<<" result="<<removed<<" foreign_lost="<<otherLost<<'\n';
  Check(swapAttempted&&!otherLost,"Cleanup never erases an independent client substituted during idle inspection");
  Check(swapSucceeded?!removed:removed,"Cleanup reports a changed root or completes only its still-owned original root");
  Seed(leaf);fs::create_directory(target);Put(foreignRoot/L"junction-sentinel","JUNCTION TARGET");
  if(!CtTest::CreateDirectoryJunction(target/L"linked",foreignRoot))throw std::runtime_error("Private junction fixture unavailable");
  const bool junctionRemoved=RemoveTransientProfileIfIdleAndVerify(target,error);
  Check(Read(foreignRoot/L"junction-sentinel")=="JUNCTION TARGET","Transient cleanup never follows nested junctions into another client");
  std::cout<<"TRANSIENT_JUNCTION removed="<<junctionRemoved<<'\n';
 }
}
static void CreationCases(){
 for(BrowserKind browser:{BrowserKind::Edge,BrowserKind::Chrome,BrowserKind::Brave,BrowserKind::Firefox})for(bool isDefault:{false,true}){
  if(isDefault&&browser==BrowserKind::Firefox)continue;Seed(isDefault?L"Default":L"Temp");
  fs::copy_file(fixture.parent_path().parent_path().parent_path()/L"Default.7z",g_sDataDir/L"Default.7z");
  collisionDuringCreate=true;const unsigned launchBefore=launchRequests;
  LaunchProfileAsync(isDefault?L"Default":L"Temp",!isDefault,isDefault,browser);AwaitWorkerCompletionBeforeJoin();
  std::cout<<"TRANSIENT_CREATE browser="<<static_cast<int>(browser)<<" default="<<isDefault<<" collision="<<collisionHit<<" preserved="<<(Read(target/L"foreign.txt")=="INDEPENDENT FOLDER")<<'\n';
  Check(collisionHit&&Read(target/L"foreign.txt")=="INDEPENDENT FOLDER","Failed transient creation never cleans a directory this launch did not create");
  Check(launchRequests==launchBefore&&!g_isLaunchInFlight.load()&&g_activeProfiles.empty(),"Creation rejection starts no browser and releases launch bookkeeping");
  Check(Read(foreignRoot/L"Cookies")=="OTHER CLIENT COOKIES","Creation failure preserves unrelated browser bytes");
 }
 Seed(L"Default");BrowserKind browser=BrowserKind::Firefox;bool fresh=true;HANDLE process=reinterpret_cast<HANDLE>(1);std::wstring error;
 Check(!LaunchProfile(L"Default",false,true,browser,{},&process,nullptr,nullptr,&error,&fresh)&&!fresh&&!process&&!fs::exists(target),"Firefox Default editor refusal creates no transient profile or process");
}
static void LaunchRollbackSubstitutionCase(){
 Seed(L"Temp");g_sFirefoxPath=g_sDataDir/L"firefox.exe";Put(g_sFirefoxPath,"PRIVATE SENTINEL: NOT EXECUTABLE");
 const unsigned requestsBefore=blockedCreateRequests,preparedBefore=preparedProcessRequests;
 swapDuringProcessCreate=true;
 LaunchProfileAsync(L"Temp",true,false,BrowserKind::Firefox);
 AwaitWorkerCompletionBeforeJoin();
 const bool independentPreserved=(Read(foreignRoot/L"notes.txt")=="OTHER CLIENT NOTES"&&Read(foreignRoot/L"Cookies")=="OTHER CLIENT COOKIES")||
  (Read(target/L"notes.txt")=="OTHER CLIENT NOTES"&&Read(target/L"Cookies")=="OTHER CLIENT COOKIES");
 std::cout<<"TRANSIENT_LAUNCH_ROLLBACK attempted="<<swapAttempted<<" substituted="<<swapSucceeded<<" foreign_preserved="<<independentPreserved<<" blocked_create_requests="<<(blockedCreateRequests-requestsBefore)<<'\n';
 Check(processCreateHit&&swapAttempted&&blockedCreateRequests==requestsBefore+1&&preparedProcessRequests==preparedBefore+1,"Actual prepared Firefox Temp worker reaches exactly one intercepted CreateProcess failure boundary");
 Check(independentPreserved,"Failed launch rollback preserves independent client notes/cookies substituted after successful Temp creation");
 Check(!swapSucceeded||fs::exists(preservedRoot/L"user.js"),"A successful substitution retains the prepared original Temp profile for ownership comparison");
 Check(g_activeProfiles.empty()&&g_sessions.empty(),"Intercepted process creation failure registers no active profile or session");
 Check(Read(g_sFirefoxPath)=="PRIVATE SENTINEL: NOT EXECUTABLE","Intercepted process request leaves its private executable sentinel unchanged");
}
static void OwnedPreparedRollbackControls(){
 for(const auto&mode:std::vector<std::pair<BrowserKind,bool>>{{BrowserKind::Firefox,false},{BrowserKind::Chrome,false},{BrowserKind::Chrome,true}}){
  const auto browser=mode.first;const bool isDefault=mode.second;Seed(isDefault?L"Default":L"Temp");
  const fs::path executable=g_sDataDir/(browser==BrowserKind::Firefox?L"firefox.exe":L"chrome.exe");Put(executable,"PRIVATE SENTINEL: NOT EXECUTABLE");
  if(browser==BrowserKind::Firefox)g_sFirefoxPath=executable;else g_sChromePath=executable;
  const auto archive=g_sDataDir/L"Default.7z";fs::copy_file(fixture.parent_path().parent_path().parent_path()/L"Default.7z",archive);const auto archiveBefore=Read(archive);
  const unsigned requestsBefore=blockedCreateRequests,preparedBefore=preparedProcessRequests;
  LaunchProfileAsync(isDefault?L"Default":L"Temp",!isDefault,isDefault,browser);AwaitWorkerCompletionBeforeJoin();
  std::cout<<"TRANSIENT_OWNED_ROLLBACK browser="<<static_cast<int>(browser)<<" default="<<isDefault<<" prepared="<<(preparedProcessRequests-preparedBefore)<<" removed="<<!fs::exists(target)<<'\n';
  Check(blockedCreateRequests==requestsBefore+1&&preparedProcessRequests==preparedBefore+1,"Owned Firefox or Chromium profile prepares fully before its intercepted process-creation failure");
  Check(!fs::exists(target),"Known-owned prepared Temp or Default profile is removed after process-creation failure");
  Check(g_activeProfiles.empty()&&g_sessions.empty(),"Owned failed launch retains no active profile or session");
  Check(Read(foreignRoot/L"notes.txt")=="OTHER CLIENT NOTES"&&Read(foreignRoot/L"Cookies")=="OTHER CLIENT COOKIES","Known-owned rollback preserves independent private client notes and cookies");
  Check(Read(executable)=="PRIVATE SENTINEL: NOT EXECUTABLE"&&Read(archive)==archiveBefore,"Known-owned rollback preserves private executable sentinel and exact starter archive bytes");
 }
}
static void ReservationOwnershipCase(){
 Seed(L"Temp");g_sFirefoxPath=g_sDataDir/L"firefox.exe";Put(g_sFirefoxPath,"PRIVATE SENTINEL: NOT EXECUTABLE");
 swapAfterDirectoryCreation=true;
 LaunchProfileAsync(L"Temp",true,false,BrowserKind::Firefox);AwaitWorkerCompletionBeforeJoin();
 const bool foreignKept=(Read(foreignRoot/L"notes.txt")=="OTHER CLIENT NOTES"&&Read(foreignRoot/L"Cookies")=="OTHER CLIENT COOKIES")||
  (Read(target/L"notes.txt")=="OTHER CLIENT NOTES"&&Read(target/L"Cookies")=="OTHER CLIENT COOKIES");
 std::cout<<"TRANSIENT_RESERVATION attempted="<<swapAttempted<<" substituted="<<swapSucceeded<<" FOREIGN_DATA_LOST="<<!foreignKept<<'\n';
 Check(reservationSwapHit&&swapAttempted,"Substitution reaches the successful disposable-directory creation boundary before capture");
 Check(foreignKept,"Failed Temp reservation never claims and deletes an independent substituted client");
 Check(g_activeProfiles.empty()&&g_sessions.empty()&&!g_isLaunchInFlight.load(),"Reservation ownership failure releases active/session/worker state");
}
static void ReservedStarterControls(){
 for(unsigned mode=0;mode<3;++mode){
  Seed(L"Temp");fs::copy_file(fixture.parent_path().parent_path().parent_path()/L"Default.7z",g_sDataDir/L"Default.7z");
  std::shared_ptr<transient_profile::Identity> identity;
  if(mode==2){identity=transient_profile::Identity::Create(target);if(!identity)throw std::runtime_error("Private reserved starter control failed to create its owned directory");}
  else fs::create_directory(target);
  if(mode==1){Put(target/L"~ctSpaces-reservation","");identity=transient_profile::Identity::Capture(target);}
  else Put(target/L"private-existing-content","PRIVATE CONTENT MUST SURVIVE");
  const auto sentinel=mode==1?target/L"~ctSpaces-reservation":target/L"private-existing-content";
  const auto before=Read(sentinel);
  Check(!extDef(target,L"Private nonempty starter control",identity),"Starter refuses generic nonempty, imitation reservation, or extra content beside an owned reservation");
  Check(fs::exists(sentinel)&&Read(sentinel)==before&&!fs::exists(target/L"ctSpaces")&&!fs::exists(target/L"Default"),"Refused starter preserves exact existing content and writes no profile payload");
  Check(Read(foreignRoot/L"notes.txt")=="OTHER CLIENT NOTES"&&Read(foreignRoot/L"Cookies")=="OTHER CLIENT COOKIES","Strict reserved-directory controls preserve independent client data");
 }
}
template<class ProfileInfo>
static void CaptureTrackedDirectory(ProfileInfo&info,const fs::path&path){
 // Earlier frozen sources lack the sixth field; the current production test
 // must retain the same original directory owner that real launches register.
 if constexpr(requires(ProfileInfo&value){value.directory;}){
  using DirectoryIdentity=typename decltype(info.directory)::element_type;
  info.directory=DirectoryIdentity::Capture(path);
  if(!info.directory)throw std::runtime_error("Tracked exit directory identity could not be captured");
 }
}
static void ExitCases(){
 for(const auto type:{ProfileType::Temporary,ProfileType::Default}){
  Seed(type==ProfileType::Temporary?L"Temp":L"Default");Put(target/L"state.txt","EXIT STATE");
  const std::wstring name=target.filename().wstring();ActiveProfileInfo info{};info.pid=0x7fffa201;info.profilePath=target;info.type=type;
  CaptureTrackedDirectory(info,target);
  g_activeProfiles.emplace(ClientBrowserKey{name,BrowserKind::Edge},info);
  Check(!HandleProfileExitOnUiThread({info.pid+1,name,BrowserKind::Edge,type,false,false})&&g_activeProfiles.size()==1&&fs::exists(target),"Stale transient exit PID cannot claim or clean a newer tracked session");
  Check(!HandleProfileExitOnUiThread({info.pid,name,BrowserKind::Firefox,type,false,false})&&g_activeProfiles.size()==1,"Wrong-browser transient exit cannot claim the current session");
  Check(HandleProfileExitOnUiThread({info.pid,name,BrowserKind::Edge,type,true,false})&&g_activeProfiles.empty()&&!fs::exists(target),"Matching launch-rollback exit cleans the correct disposable profile without Default save prompt");
  Check(!HandleProfileExitOnUiThread({info.pid,name,BrowserKind::Edge,type,true,false}),"Duplicate exit notification cannot repeat cleanup");
  Seed(type==ProfileType::Temporary?L"Temp":L"Default");Put(target/L"state.txt","NORMAL EXIT");info.profilePath=target;
  CaptureTrackedDirectory(info,target);
  g_activeProfiles.emplace(ClientBrowserKey{target.filename().wstring(),BrowserKind::Edge},info);
  Check(HandleProfileExitOnUiThread({info.pid,target.filename().wstring(),BrowserKind::Edge,type,false,false})&&!fs::exists(target),"Normal transient exit cleans Temp or follows the intercepted Default discard choice");
  Seed(type==ProfileType::Temporary?L"Temp":L"Default");Put(target/L"state.txt","TRACKED ORIGINAL");Put(g_sDataDir/L"Default.7z","PREVIOUS STARTER ARCHIVE");info.profilePath=target;
  CaptureTrackedDirectory(info,target);
  const std::wstring substitutedName=target.filename().wstring();g_activeProfiles.emplace(ClientBrowserKey{substitutedName,BrowserKind::Edge},info);
  fs::rename(target,preservedRoot);fs::rename(foreignRoot,target);
  const bool claimed=HandleProfileExitOnUiThread({info.pid,substitutedName,BrowserKind::Edge,type,false,false});
  std::cout<<"TRANSIENT_TRACKED_EXIT type="<<(type==ProfileType::Temporary?"Temp":"Default")<<" claimed="<<claimed<<" foreign_preserved="<<(Read(target/L"notes.txt")=="OTHER CLIENT NOTES"&&Read(target/L"Cookies")=="OTHER CLIENT COOKIES")<<'\n';
  Check(claimed&&g_activeProfiles.empty(),"Matching substituted normal exit claims only the original active-session bookkeeping");
  Check(Read(target/L"notes.txt")=="OTHER CLIENT NOTES"&&Read(target/L"Cookies")=="OTHER CLIENT COOKIES","Actual active-map directory identity prevents normal Temp/Default exit from deleting substituted client data");
  Check(Read(preservedRoot/L"state.txt")=="TRACKED ORIGINAL","Rejected substituted normal exit retains the original tracked directory bytes");
  Check(g_bUiEnabled&&Read(g_sDataDir/L"Default.7z")=="PREVIOUS STARTER ARCHIVE","Rejected substituted exit preserves the previous starter and restores UI availability");
 }
}
int wmain(int argc,wchar_t**argv){
 try{fixture=retained_fixture::CreateRoot(fs::current_path()/L"build/app-audit-round8-2026-10-15",L"transient");
  g_hInst=GetModuleHandleW(nullptr);
  const bool creationOnly=argc==2&&wcscmp(argv[1],L"--creation-only")==0;
  const bool rollbackOnly=argc==2&&wcscmp(argv[1],L"--rollback-substitution-only")==0;
  const bool ownershipOnly=argc==2&&wcscmp(argv[1],L"--launch-ownership-only")==0;
  const bool reservationOnly=argc==2&&wcscmp(argv[1],L"--reservation-only")==0;
  if(reservationOnly)ReservationOwnershipCase();else if(creationOnly)CreationCases();else if(rollbackOnly)LaunchRollbackSubstitutionCase();else if(ownershipOnly){LaunchRollbackSubstitutionCase();OwnedPreparedRollbackControls();}else{CleanupCases();CreationCases();LaunchRollbackSubstitutionCase();OwnedPreparedRollbackControls();ExitCases();ReservationOwnershipCase();ReservedStarterControls();}
  Check(presentation==0,"Transient fixture requests no foreground, focus, window display or positioning");
  const unsigned expectedBlocked=creationOnly?0u:(rollbackOnly||reservationOnly)?1u:ownershipOnly?4u:5u;
  Check(launchRequests==blockedCreateRequests&&blockedCreateRequests==expectedBlocked,"Only expected process-creation failure requests reach the unconditionally blocked boundary; no Shell request");
  std::cout<<"Transient lifecycle: "<<checks<<" checks, "<<caseNumber<<" private cases, "<<failures<<" failures; blocked_create_requests="<<blockedCreateRequests<<"; HWNDs=0; no actual app/browser launches or clipboard access.\n";
  return failures?1:0;
 }catch(const std::exception&error){std::cerr<<"Transient fixture error: "<<error.what()<<'\n';return 2;}
}
