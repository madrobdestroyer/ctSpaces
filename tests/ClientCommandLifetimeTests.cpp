// Exact production rename/archive commands with modal choices intercepted.
// All directories belong to this process's disposable fixture.
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <iostream>
#include <functional>
#include <stdexcept>
static unsigned presentation=0,launches=0,forbidden=0;
static BOOL WINAPI NoShow(HWND,int){++presentation;return FALSE;}
static HWND WINAPI NoFocus(HWND){++presentation;return nullptr;}
static BOOL WINAPI NoForeground(HWND){++presentation;return FALSE;}
static BOOL WINAPI NoPosition(HWND,HWND,int,int,int,int,UINT){++presentation;return FALSE;}
static BOOL WINAPI NoPaint(HWND,const RECT*,BOOL){return FALSE;}
static BOOL WINAPI NoRedraw(HWND,const RECT*,HRGN,UINT){return FALSE;}
static BOOL WINAPI NoPost(HWND w,UINT,WPARAM,LPARAM){DWORD pid=0;if(!w||!IsWindow(w)||!GetWindowThreadProcessId(w,&pid)||pid!=GetCurrentProcessId())++forbidden;return FALSE;}
static HINSTANCE WINAPI NoShell(HWND,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,INT){++launches;return reinterpret_cast<HINSTANCE>(ERROR_ACCESS_DENIED);}
static BOOL WINAPI NoShellEx(SHELLEXECUTEINFOW*){++launches;return FALSE;}
static BOOL WINAPI NoProcess(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION){++launches;return FALSE;}
static BOOL WINAPI NoTray(DWORD,PNOTIFYICONDATAW){++launches;return FALSE;}
static BOOL WINAPI NoClipboard(HWND){++forbidden;return FALSE;}
static LSTATUS WINAPI NoRegistry(HKEY,LPCWSTR,LPCWSTR,LPDWORD,LPVOID,LPDWORD){++forbidden;return ERROR_ACCESS_DENIED;}
static HANDLE WINAPI Snapshot(DWORD,DWORD){return CreateEventW(nullptr,FALSE,FALSE,nullptr);}
static BOOL WINAPI First(HANDLE,LPPROCESSENTRY32W p){wcscpy_s(p->szExeFile,L"private-command-fixture.exe");p->th32ProcessID=GetCurrentProcessId();return TRUE;}
static BOOL WINAPI Next(HANDLE,LPPROCESSENTRY32W){SetLastError(ERROR_NO_MORE_FILES);return FALSE;}
#define ShowWindow NoShow
#define SetFocus NoFocus
#define SetForegroundWindow NoForeground
#define SetActiveWindow NoFocus
#define BringWindowToTop NoForeground
#define SetWindowPos NoPosition
#define InvalidateRect NoPaint
#define RedrawWindow NoRedraw
#define PostMessageW NoPost
#define ShellExecuteW NoShell
#define ShellExecuteExW NoShellEx
#define CreateProcessW NoProcess
#define Shell_NotifyIconW NoTray
#define OpenClipboard NoClipboard
#define SHGetValueW NoRegistry
#define CreateToolhelp32Snapshot Snapshot
#define Process32FirstW First
#define Process32NextW Next
#define CTSPACES_INSTALLER_TEST_HOOKS
#define GuiRenameClient IncludedRenameNotRun
#define GuiArchiveClient IncludedArchiveNotRun
#define wWinMain ProductionEntryNotRun
#ifdef CTSPACES_CLIENT_COMMAND_BEFORE
#include "../build/app-audit-round11-2026-10-15/archive-clients/ctSpaces-before.cpp"
#else
#include "../ctSpaces.cpp"
#endif
#undef GuiRenameClient
#undef GuiArchiveClient
#undef wWinMain
static unsigned checks=0,failures=0,scenarios=0,inputs=0,confirmations=0;
static fs::path fixture,target,foreign,aside;
enum class Mutation{None,Root,Parent};
static Mutation mutation=Mutation::None;
static bool attempted=false,moved=false,cancelInput=false;
static DWORD moveError=0;
static int answer=IDYES;
static std::wstring renamed=L"Renamed";
static void Check(bool ok,const char*text){++checks;if(!ok)++failures;std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';}
static void Put(const fs::path&p,const std::string&data){fs::create_directories(p.parent_path());std::ofstream out(p,std::ios::binary);out<<data;if(!out)throw std::runtime_error("Private write failed");}
static std::string Read(const fs::path&p){std::ifstream in(p,std::ios::binary);if(!in)return{};return {std::istreambuf_iterator<char>(in),{}};}
static void Swap(){
 if(mutation==Mutation::None)return;attempted=true;
 const auto from=mutation==Mutation::Parent?target.parent_path():target;
 moved=MoveFileExW(from.c_str(),aside.c_str(),0)!=FALSE;moveError=moved?ERROR_SUCCESS:GetLastError();
 if(moved){
  if(mutation==Mutation::Parent){fs::create_directory(target.parent_path());fs::copy(aside/L"Bravo",target,fs::copy_options::recursive);}
  else if(!MoveFileExW(foreign.c_str(),target.c_str(),0))throw std::runtime_error("Private replacement failed");
 }
}
struct PrivateInput{static std::optional<std::wstring> Show(HWND,const std::wstring&,const std::wstring&,const std::wstring&){++inputs;Swap();return cancelInput?std::nullopt:std::optional<std::wstring>(renamed);}};
#define InputBoxWindow PrivateInput
#ifdef CTSPACES_CLIENT_COMMAND_BEFORE
#include "../build/app-audit-round11-2026-10-15/archive-clients/commands-before.inl"
#else
#include "../build/tests/ClientCommandLifetime/generated-commands.inl"
#endif
#undef InputBoxWindow
int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR text,LPCWSTR,UINT flags){std::wcout<<L"NOTICE "<<(text?text:L"")<<L'\n';if((flags&MB_YESNO)==MB_YESNO){++confirmations;Swap();return answer;}return IDOK;}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path&,std::wstring&){return false;}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path&){}
static void Seed(Mutation next){
 g_sDataDir=fixture/std::to_wstring(++scenarios);fs::create_directory(g_sDataDir);g_sConfigPath=g_sDataDir/L"config.ini";
 target=g_sDataDir/L"Sites/Alpha";foreign=g_sDataDir/L"Sites/Bravo";aside=g_sDataDir/L"Displaced";
 for(const auto&[name,body]:std::vector<std::pair<std::wstring,std::string>>{{L"Alpha","ALPHA"},{L"Bravo","BRAVO"}}){const auto root=g_sDataDir/L"Sites"/name;Put(root/kClientSchemaMarkerName,kClientSchemaMarkerText);Put(root/L"private-note",body);const auto slot=root/L"Browsers/edge";Put(slot/kBrowserSchemaMarkerName,GetBrowserMarkerText(BrowserKind::Edge));Put(slot/L"Profile/ctSpaces","ctSpaces-profile=2\r\n");Put(slot/L"Profile/Default/Preferences","{}");client_notes::NotePage note{L"Ticket",true,"{\\rtf1 "+body+"}",{}};note.id=1;note.ticket=name+L" ticket";if(!client_notes::WriteNotebook(root,{note},client_notes::ReadNotebook(root)))throw std::runtime_error("Private notebook seed failed");}
 g_sShortcutDesktopOverride=g_sDataDir/L"Desktop";fs::create_directory(g_sShortcutDesktopOverride);
 g_pinnedClients={L"Alpha",L"Bravo"};g_archivedClients.clear();g_sessions.clear();g_activeProfiles.clear();g_clientsWithoutTabRestore.clear();g_allClientNames.clear();g_iSelectedSessionTab=0;
 g_bUiEnabled=true;g_bClientNotesBusy=false;g_bDefaultProfileUiBusy=false;g_isLaunchInFlight=false;g_bCleanupBusy=false;g_bCloseAllClientsPending=false;
 SetWindowTextW(g_hClientEdit,L"Alpha");mutation=next;attempted=moved=cancelInput=false;moveError=0;answer=IDYES;inputs=confirmations=0;renamed=L"Renamed";
 if(!SavePinnedClients()||!SaveArchivedClients())throw std::runtime_error("Private membership seed failed");
}
static void Audit(){if(IsWindowVisible(g_hGui))throw std::runtime_error("Private owner became visible");GUITHREADINFO ui{sizeof(ui)};if(!GetGUIThreadInfo(GetCurrentThreadId(),&ui)||ui.hwndActive||ui.hwndFocus||ui.hwndCapture||launches||forbidden)throw std::runtime_error("Background boundary violated");}
int wmain(){try{std::cout<<std::unitbuf;CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);g_hInst=GetModuleHandleW(nullptr);g_bQaInstance=true;g_sExeDir=GetCurrentExecutablePath().parent_path();
 // Keep fixture roots short enough for production's browser descendant budget.
 fixture=fs::current_path()/L"build"/(L"ccl-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));if(!fs::create_directory(fixture))throw std::runtime_error("Fresh fixture unavailable");
 g_hGui=CreateWindowExW(0,L"STATIC",L"Private command owner",0,0,0,1,1,HWND_MESSAGE,nullptr,g_hInst,nullptr);
 g_hClientEdit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD,0,0,1,1,g_hGui,nullptr,g_hInst,nullptr);
 g_hComboClient=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|CBS_DROPDOWN,0,0,1,1,g_hGui,nullptr,g_hInst,nullptr);
 if(!g_hGui||!g_hClientEdit||!g_hComboClient)throw std::runtime_error("Private controls unavailable");
 for(const auto point:{Mutation::Root,Mutation::Parent}){
  Seed(point);GuiRenameClient();Check(inputs==1&&attempted,"Rename attack reaches the exact production naming prompt");Check(!moved&&(moveError==ERROR_SHARING_VIOLATION||moveError==ERROR_ACCESS_DENIED),"Rename keeps the original client and Sites ancestor bound through naming");Check(Read(g_sDataDir/L"Sites/Renamed/private-note")=="ALPHA","Rename moves the intended client notebook identity");Check(Read(foreign/L"private-note")=="BRAVO","Rename leaves the independent client at its original identity");Audit();
  Seed(point);GuiArchiveClient();Check(confirmations==1&&attempted,"Archive attack reaches the exact production confirmation");Check(!moved&&(moveError==ERROR_SHARING_VIOLATION||moveError==ERROR_ACCESS_DENIED),"Archive keeps the original client and Sites ancestor bound through confirmation");Check(Read(target/L"private-note")=="ALPHA"&&Read(foreign/L"private-note")=="BRAVO","Archive preserves both notebooks at their original client identities");Check(g_archivedClients==std::vector<std::wstring>{L"Alpha"}&&g_pinnedClients==std::vector<std::wstring>{L"Bravo"},"Archive commits only the selected original client's membership");Audit();
 }
 Seed(Mutation::None);GuiRenameClient();Check(Read(g_sDataDir/L"Sites/Renamed/private-note")=="ALPHA"&&Read(foreign/L"private-note")=="BRAVO","Ordinary rename succeeds with original data");Audit();
 Seed(Mutation::None);cancelInput=true;const auto cfg=Read(g_sConfigPath);GuiRenameClient();Check(Read(target/L"private-note")=="ALPHA"&&!fs::exists(g_sDataDir/L"Sites/Renamed")&&Read(g_sConfigPath)==cfg,"Naming cancellation preserves exact original data and config");Audit();
 Seed(Mutation::None);answer=IDNO;const auto cfg2=Read(g_sConfigPath);GuiArchiveClient();Check(g_archivedClients.empty()&&Read(g_sConfigPath)==cfg2,"Archive cancellation preserves exact membership bytes");Audit();
 Seed(Mutation::None);renamed=L"ALPHA";GuiRenameClient();Check(HasExactSafeDirectoryEntryName(g_sDataDir/L"Sites/ALPHA")&&Read(target/L"private-note")=="ALPHA","Case-only rename commits exact requested spelling through supplied-guard upgrade");Check(g_pinnedClients==std::vector<std::wstring>{L"ALPHA",L"Bravo"},"Case-only rename updates exact pinned spelling");Audit();
 Seed(Mutation::None);const auto configBeforeRename=Read(g_sConfigPath),notebookBeforeRename=Read(target/client_notes::kNotebookFileName);HANDLE locked=CreateFileW(g_sConfigPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(locked==INVALID_HANDLE_VALUE)throw std::runtime_error("Private rename lock unavailable");GuiRenameClient();CloseHandle(locked);Check(Read(g_sConfigPath)==configBeforeRename&&Read(target/client_notes::kNotebookFileName)==notebookBeforeRename&&!fs::exists(g_sDataDir/L"Sites/Renamed")&&g_pinnedClients==std::vector<std::wstring>{L"Alpha",L"Bravo"},"GUI rename config-lock failure rolls back exact original root, notebook, membership and config");GuiRenameClient();Check(Read(g_sDataDir/L"Sites/Renamed"/client_notes::kNotebookFileName)==notebookBeforeRename&&g_pinnedClients==std::vector<std::wstring>{L"Renamed",L"Bravo"},"GUI rename clean retry commits after releasing the config lock");Audit();
 Seed(Mutation::None);const auto configBeforeArchive=Read(g_sConfigPath),notebookBeforeArchive=Read(target/client_notes::kNotebookFileName);locked=CreateFileW(g_sConfigPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(locked==INVALID_HANDLE_VALUE)throw std::runtime_error("Private archive lock unavailable");GuiArchiveClient();CloseHandle(locked);Check(Read(g_sConfigPath)==configBeforeArchive&&Read(target/client_notes::kNotebookFileName)==notebookBeforeArchive&&g_archivedClients.empty()&&g_pinnedClients==std::vector<std::wstring>{L"Alpha",L"Bravo"},"GUI archive config-lock failure preserves exact notebook, membership and config");GuiArchiveClient();Check(Read(target/client_notes::kNotebookFileName)==notebookBeforeArchive&&g_archivedClients==std::vector<std::wstring>{L"Alpha"}&&g_pinnedClients==std::vector<std::wstring>{L"Bravo"},"GUI archive clean retry commits after releasing the config lock");Audit();
#ifndef CTSPACES_CLIENT_COMMAND_BEFORE
 // Contract checks for the new internal supplied-guard parameter. The baseline
 // has no such API; these are repair robustness controls, not extra findings.
 for(bool renameOperation:{true,false}){
  Seed(Mutation::None);const auto originalConfig=Read(g_sConfigPath),alpha=Read(target/client_notes::kNotebookFileName),bravo=Read(foreign/client_notes::kNotebookFileName);
  client_deletion_safety::BoundTree wrongClient;std::wstring error;if(!wrongClient.CaptureNamespace(foreign,error))throw std::runtime_error("Private mismatched guard unavailable");
  const bool accepted=renameOperation?RenameClientProfile(L"Alpha",L"Renamed",error,&wrongClient):ArchiveClientProfile(L"Alpha",error,&wrongClient);
  Check(!accepted&&!error.empty()&&Read(g_sConfigPath)==originalConfig&&Read(target/client_notes::kNotebookFileName)==alpha&&Read(foreign/client_notes::kNotebookFileName)==bravo&&!fs::exists(g_sDataDir/L"Sites/Renamed")&&g_archivedClients.empty()&&g_pinnedClients==std::vector<std::wstring>{L"Alpha",L"Bravo"},renameOperation?"Mismatched supplied rename guard is rejected before touching either client's data or config":"Mismatched supplied archive guard is rejected before touching either client's data or config");Audit();
 }
#endif
 std::cout<<"Client command lifetime: "<<checks<<" checks, "<<scenarios<<" scenarios, "<<failures<<" failures; native input/foreground/launches=0\n";std::wcout<<L"Private fixture retained: "<<fixture<<L'\n';
 DestroyWindow(g_hGui);g_hGui=nullptr;CoUninitialize();return failures?1:0;
 }catch(const std::exception&error){std::cerr<<"Client command lifetime fixture failed: "<<error.what()<<'\n';if(g_hGui)DestroyWindow(g_hGui);CoUninitialize();return 2;}}
