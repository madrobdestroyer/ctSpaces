// Use the existing reviewed hidden-window, clipboard, input and native-event
// guards. This entry never calls its foreground/default test entry point.
#include <windows.h>
static BOOL WINAPI LifetimeSilentBeep(UINT){return TRUE;}
static unsigned lifetimeQuitRequests=0;
static void WINAPI LifetimePostQuit(int code){++lifetimeQuitRequests;::PostQuitMessage(code);}
#define MessageBeep LifetimeSilentBeep
#define PostQuitMessage LifetimePostQuit
#ifdef CTSPACES_LIFETIME_BASELINE
#define CTSPACES_NOTES_PRODUCTION_SOURCE "C:/Projects/ctSpaces/build/notes-independent-lifetime-2026-10-16/ctSpaces-before.cpp"
#endif
#define wmain NotesWorkspaceUnusedMain
#include "NotesWorkspaceTests.cpp"
#undef wmain
#undef MessageBeep
#undef PostQuitMessage

namespace notes_launcher_lifetime {
static unsigned assertions=0,failures=0,scenarios=0;
static constexpr UINT explicitExit=WM_APP+26;
static constexpr wchar_t windowClass[]=L"ctSpacesHiddenLauncherLifetime";
static void Expect(bool value,const char *name) {
  ++assertions;if(!value) ++failures;
  std::cout<<(value ? "PASS " : "FAIL ")<<name<<std::endl;
}
static bool TakeQuit() {
  MSG message{};return PeekMessageW(&message,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)!=FALSE;
}
static HWND Launcher() {
  ++scenarios;TakeQuit();
  BuildThemeColors(true,g_themeColors);UpdateThemeBrushes();
  g_bUiEnabled=true;g_bClosePendingDuringLaunch=false;g_bExitWhenProfilesClose=false;
#ifndef CTSPACES_LIFETIME_BASELINE
  g_bExplicitExitPendingDuringLaunch=false;
#endif
  g_bCloseAllClientsPending=false;g_bArchiveTaskInProgress=false;g_minimizeToTray=false;
  g_launcherInTray=false;g_launcherTrayIcon=false;trayAddWorks=true;trayVersionWorks=true;
  g_bClientNotesBusy.store(false);g_bCleanupBusy.store(false);g_isLaunchInFlight.store(false);
  g_sessions.clear();g_iSelectedSessionTab=0;g_activeProfiles.clear();
  WNDCLASSW type{};type.hInstance=g_hInst;type.lpfnWndProc=WndProc;type.lpszClassName=windowClass;
  RegisterClassW(&type);
  g_hGui=CreateWindowExW(WS_EX_NOACTIVATE,windowClass,L"Hidden launcher lifetime",WS_OVERLAPPEDWINDOW,
      0,0,640,480,nullptr,nullptr,g_hInst,nullptr);
  Check(g_hGui && IsWindow(g_hGui),"Private hidden production launcher created");
  const auto button=[&](const wchar_t *label,int id){return CreateWindowExW(0,L"BUTTON",label,WS_CHILD|BS_OWNERDRAW,0,0,80,28,g_hGui,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),g_hInst,nullptr);};
  g_hBtnNotes=button(L"Notes",IDC_BTN_NOTES);g_hBtnTmpProf=button(L"Default",IDC_BTN_TEMP);g_hBtnConfig=button(L"Options",IDC_BTN_CONFIG);
  g_hClientEdit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL,0,32,160,28,g_hGui,reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CLIENT_EDIT)),g_hInst,nullptr);
  Check(g_hBtnNotes && g_hBtnTmpProf && g_hBtnConfig && g_hClientEdit,"Private native launcher action controls exist for state and session redraw checks");
  return g_hGui;
}
static HWND Notes(const wchar_t *name) {
  Fixture(name);ShowClientNotes(name);Check(g_hNotesWindow && IsWindow(g_hNotesWindow),"Normal production Notes opening completes");
  return g_hNotesWindow;
}
static void Finish(HWND launcher) {
  if(g_hNotesWindow && IsWindow(g_hNotesWindow)) DestroyWindow(g_hNotesWindow);
  if(IsWindow(launcher)) DestroyWindow(launcher);
  TakeQuit();g_hGui=nullptr;g_hNotesWindow=nullptr;
  NotesBackgroundAudit("independent launcher lifetime cleanup");
}
static void CloseAndAutosave() {
  HWND launcher=Launcher(),notes=Notes(L"Independent close");
  Expect(!GetWindow(notes,GW_OWNER),"Notes has no launcher owner and can minimize independently");
  Expect(!(GetWindowLongPtrW(notes,GWL_EXSTYLE)&WS_EX_TOOLWINDOW),"Notes remains eligible for its own taskbar button");
  Type(notes,L"Saved before launcher close");Save(notes);Type(notes,L"; pending work");
  const auto editor=Edit(notes);RECT before{};GetWindowRect(notes,&before);notesShowRequests.clear();
  SendMessageW(launcher,WM_CLOSE,0,0);
  Expect(IsWindow(launcher),"Launcher close retains the process message window while Notes is open");
  Expect(IsWindow(notes) && g_hNotesWindow==notes,"Launcher close keeps the same Notes window open");
  Expect(g_launcherInTray && g_launcherTrayIcon,"Launcher close uses the existing recoverable tray lifetime");
  Expect(!TakeQuit(),"Closing only the launcher does not queue application termination");
  if(IsWindow(notes)) {
    RECT after{};GetWindowRect(notes,&after);
    Expect(EqualRect(&before,&after) && Edit(notes)==editor,"Launcher close retains Notes placement and editor identity");
    SendMessageW(notes,WM_TIMER,1,0);
    const auto saved=client_notes::ReadNotebook(State(notes).clientRoot);
    Expect(!State(notes).saveFailed && std::any_of(saved.pages.begin(),saved.pages.end(),[](const auto &page){return NotesPlain(page).find(L"pending work")!=std::wstring::npos;}),"Notes autosave still commits pending work after launcher close");
    SendMessageW(launcher,WM_APP_SHOW_LAUNCHER,0,0);
    GetWindowRect(notes,&after);
    Expect(!g_launcherInTray && !g_launcherTrayIcon && g_hNotesWindow==notes && EqualRect(&before,&after),"Showing the retained launcher leaves Notes in the same place");
    SendMessageW(launcher,WM_CLOSE,0,0);SendMessageW(notes,WM_CLOSE,0,0);
    Expect(!g_hNotesWindow && IsWindow(launcher) && g_launcherInTray && !TakeQuit(),"Closing the last Notes window retains the deliberate tray lifetime");
  }
  Finish(launcher);
}
#ifndef CTSPACES_LIFETIME_BASELINE
static void MinimizeAndBusy() {
  HWND launcher=Launcher(),notes=Notes(L"Independent minimize");
  const auto editor=Edit(notes);RECT before{};GetWindowRect(notes,&before);
  for(bool tray:{false,true}) {
    g_minimizeToTray=tray;notesShowRequests.clear();
    SendMessageW(launcher,WM_SIZE,SIZE_MINIMIZED,MAKELPARAM(640,480));
    SendMessageW(launcher,WM_SIZE,SIZE_MAXIMIZED,MAKELPARAM(1280,900));
    RECT after{};GetWindowRect(notes,&after);
    Expect(IsWindow(notes) && EqualRect(&before,&after) && Edit(notes)==editor,"Launcher minimize/maximize leaves Notes placement and editor intact");
    Expect(std::none_of(notesShowRequests.begin(),notesShowRequests.end(),[&](const auto &request){return request.window==notes;}),"Launcher size transitions never request Notes hide/minimize/restore");
  }
  for(unsigned busy=0;busy<3;++busy) {
    g_bClientNotesBusy.store(busy==0);g_bCleanupBusy.store(busy==1);g_bArchiveTaskInProgress=busy==2;
    const auto requests=notesShowRequests.size();SendMessageW(launcher,WM_CLOSE,0,0);
    Expect(IsWindow(launcher) && IsWindow(notes) && notesShowRequests.size()==requests,"Busy operations refuse launcher close without closing or hiding Notes");
  }
  g_bClientNotesBusy.store(false);g_bCleanupBusy.store(false);g_bArchiveTaskInProgress=false;
  EnableWindow(notes,FALSE);SendMessageW(launcher,WM_CLOSE,0,0);
  Expect(IsWindow(notes) && IsWindow(launcher),"A modal Notes owner remains alive during launcher close");
  EnableWindow(notes,TRUE);Finish(launcher);
}
static void TrayFailure() {
  for(bool versionFailure:{false,true}) {
    HWND launcher=Launcher(),notes=Notes(versionFailure ? L"Tray version failure" : L"Tray add failure");
    trayAddWorks=versionFailure;trayVersionWorks=false;notesShowRequests.clear();
    SendMessageW(launcher,WM_CLOSE,0,0);
    Expect(IsWindow(launcher) && IsWindow(notes) && !g_launcherInTray && !g_launcherTrayIcon,"Tray failure preserves both usable windows without an invisible tray lifetime");
    Expect(std::any_of(notesShowRequests.begin(),notesShowRequests.end(),[&](const auto &request){return request.window==launcher && request.command==SW_MINIMIZE;}),"Unavailable tray falls back to the launcher's normal taskbar minimize");
    Finish(launcher);
  }
}
static void ExplicitExitAndFailure() {
  HWND launcher=Launcher(),notes=Notes(L"Explicit exit save gate");Type(notes,L"Original saved note");Save(notes);
  Type(notes,L"; unsaved draft");const auto root=State(notes).clientRoot;
  const auto before=client_notes::ReadNotebook(root).notebook_snapshot.bytes;
  HANDLE lock=CreateFileW((root/client_notes::kNotebookFileName).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  Check(lock!=INVALID_HANDLE_VALUE,"Private native lock blocks replacement to reproduce save failure");
  SendMessageW(launcher,explicitExit,0,0);
  Expect(IsWindow(launcher) && IsWindow(notes) && State(notes).saveFailed,"Explicit Exit refuses to destroy either window when Notes cannot save");
  Expect(client_notes::ReadNotebook(root).notebook_snapshot.bytes==before && !TakeQuit(),"Failed Exit preserves saved bytes and does not terminate the process");
  CloseHandle(lock);SendMessageW(launcher,explicitExit,0,0);
  Expect(!g_hNotesWindow && !IsWindow(notes) && !IsWindow(launcher) && TakeQuit(),"Explicit Exit saves Notes then closes both windows and queues application termination");
  const auto saved=client_notes::ReadNotebook(root);
  Expect(std::any_of(saved.pages.begin(),saved.pages.end(),[](const auto &page){return NotesPlain(page).find(L"unsaved draft")!=std::wstring::npos;}),"Successful explicit Exit durably keeps the previously pending draft");
  Finish(launcher);
}
static void ExplicitExitDuringLaunch() {
  HWND launcher=Launcher(),notes=Notes(L"Deferred explicit exit");Type(notes,L"Saved before launch completion");
  g_isLaunchInFlight.store(true);SendMessageW(launcher,explicitExit,0,0);
  Expect(IsWindow(launcher) && !g_hNotesWindow && g_bClosePendingDuringLaunch,"Explicit Exit saves Notes and waits for an in-flight launch");
  g_isLaunchInFlight.store(false);SendMessageW(launcher,WM_APP_TASK_COMPLETE,1,0);
  // A later ordinary Notes request may already be queued behind completion.
  // Retain explicit Exit intent instead of treating it as launcher-only close.
  notes=Notes(L"Notes reopened before deferred exit");
  MSG deferred{};bool found=PeekMessageW(&deferred,launcher,explicitExit,explicitExit,PM_REMOVE)!=FALSE;
  if(!found) found=PeekMessageW(&deferred,launcher,WM_CLOSE,WM_CLOSE,PM_REMOVE)!=FALSE;
  Expect(found && deferred.message==explicitExit && !g_bClosePendingDuringLaunch && !g_bExplicitExitPendingDuringLaunch,"Launch completion retains the exact explicit Exit message and clears pending flags");
  if(found) DispatchMessageW(&deferred);
  Expect(!IsWindow(launcher) && !IsWindow(notes) && !g_hNotesWindow && TakeQuit(),"Deferred explicit Exit closes a newly reopened Notes window through the save gate");
  Finish(launcher);
}
static void DefaultCompletionDuringExit() {
  HWND launcher=Launcher();
  const std::wstring client=L"Shutdown Notes client";Fixture(client);NotesRememberClient(client);
  const auto root=g_sDataDir/L"Sites"/client;
  constexpr DWORD defaultPid=0x70000001,clientPid=0x70000002;
  // These announced records deliberately have no browser/process handle.
  // The production shutdown worker therefore never enumerates browser windows
  // or obtains termination authority; stop it before its bounded grace expires.
  fs::create_directories(g_sDataDir/L"Default");Put(g_sDataDir/L"Default"/L"fixture.txt","private Default editor");
  auto directory=transient_profile::Identity::Capture(g_sDataDir/L"Default");
  Check(directory!=nullptr,"Private Default editor retains its launch directory identity");
  g_sessions={{L"Default",BrowserKind::Edge,defaultPid},{client,BrowserKind::Edge,clientPid}};
  g_activeProfiles.emplace(ClientBrowserKey{L"Default",BrowserKind::Edge},ActiveProfileInfo{defaultPid,{},g_sDataDir/L"Default",ProfileType::Default,{},directory});
  g_activeProfiles.emplace(ClientBrowserKey{client,BrowserKind::Edge},ActiveProfileInfo{clientPid,{},root/L"Browsers"/L"edge"/L"Profile",ProfileType::Standard,{},{}});
  answers.push_back(IDYES);SendMessageW(launcher,explicitExit,0,0);StopShutdownWorker();
  Expect(g_bExitWhenProfilesClose && !g_bUiEnabled && IsWindow(launcher),"Confirmed explicit Exit waits for both announced browser sessions with launcher actions disabled");
  Expect(!IsWindowEnabled(g_hBtnNotes),"The native Notes button is disabled while explicit Exit awaits browser shutdown");
  answers.push_back(IDNO);
  auto *defaultExit=new ProfileExitPayload{defaultPid,L"Default",BrowserKind::Edge,ProfileType::Default,false,true};
  SendMessageW(launcher,WM_APP_PROFILE_EXITED,0,reinterpret_cast<LPARAM>(defaultExit));
  Expect(g_bExitWhenProfilesClose && IsWindow(launcher) && g_sessions.size()==1 && g_sessions.front().pid==clientPid,"Default exit runs its production completion path while the other session keeps explicit Exit pending");
  Expect(!g_bDefaultProfileUiBusy && !g_bUiEnabled && !IsWindowEnabled(g_hBtnNotes),"Default completion cannot re-enable the launcher or Notes while explicit Exit remains pending");
  SendMessageW(launcher,WM_COMMAND,MAKEWPARAM(IDC_BTN_NOTES,BN_CLICKED),reinterpret_cast<LPARAM>(g_hBtnNotes));
  const HWND reopened=g_hNotesWindow;
  Expect(!reopened,"A queued Notes command is refused while waiting for the last browser to exit");
  if(reopened) Type(reopened,L"PENDING SHUTDOWN DRAFT MUST NOT BE LOST");
  std::cout<<"Shutdown before final exit: launcher="<<IsWindow(launcher)<<"; Notes="<<(reopened!=nullptr)<<"; exitPending="<<g_bExitWhenProfilesClose<<"; notesBusy="<<g_bClientNotesBusy.load()<<"; sessions="<<g_sessions.size()<<"; tracked="<<g_activeProfiles.size()<<std::endl;
  auto *clientExit=new ProfileExitPayload{clientPid,client,BrowserKind::Edge,ProfileType::Standard,false,true};
  const auto quitsBefore=lifetimeQuitRequests;
  SendMessageW(launcher,WM_APP_PROFILE_EXITED,0,reinterpret_cast<LPARAM>(clientExit));
  const bool quit=TakeQuit();
  const bool quitRequested=lifetimeQuitRequests>quitsBefore;
  std::cout<<"Shutdown after final exit: launcher="<<IsWindow(launcher)<<"; quit="<<quit<<"; quitRequested="<<quitRequested<<"; exitPending="<<g_bExitWhenProfilesClose<<"; sessions="<<g_sessions.size()<<"; tracked="<<g_activeProfiles.size()<<"; deferred="<<g_notesDeferredExits.size()<<std::endl;
  Expect(!IsWindow(launcher) && quitRequested,"The final announced browser exit completes the confirmed application shutdown");
  const auto saved=client_notes::ReadNotebook(root);
  const bool draftSaved=std::any_of(saved.pages.begin(),saved.pages.end(),[](const auto &page){return NotesPlain(page).find(L"PENDING SHUTDOWN DRAFT MUST NOT BE LOST")!=std::wstring::npos;});
  Expect(!reopened || draftSaved || !quitRequested,"Application termination cannot discard a Notes draft accepted after shutdown confirmation");
  Finish(launcher);
}
static void DisabledChooserGuard() {
  HWND launcher=Launcher();
  for(const auto &remembered:{std::wstring{},std::wstring(L"Missing remembered client")}) {
    WritePrivateProfileStringW(L"notes_window",L"last_client",remembered.c_str(),g_sConfigPath.c_str());
    SetClientInputText(L"");SetUiState(false);
    Expect(!IsWindowEnabled(g_hBtnNotes),"The native Notes button follows disabled launcher state without a usable remembered client");
    SendMessageW(launcher,WM_COMMAND,MAKEWPARAM(IDC_BTN_NOTES,BN_CLICKED),reinterpret_cast<LPARAM>(g_hBtnNotes));
    Expect(!g_hNotesClientPanel && !g_hNotesWindow,"A queued Notes command cannot create the client chooser while launcher actions are disabled");
    if(g_hNotesClientPanel && IsWindow(g_hNotesClientPanel))DestroyWindow(g_hNotesClientPanel);
    SetUiState(true);
    Expect(IsWindowEnabled(g_hBtnNotes),"The native Notes button is restored when the launcher is usable again");
  }
  Finish(launcher);
}
#endif
}
int wmain(int argc,wchar_t **argv) {
  using namespace notes_launcher_lifetime;
  if(argc!=2 || wcscmp(argv[1],L"--headless-background")) {std::cerr<<"Only the hidden background entry is permitted; native windows created=0\n";return 2;}
  try {
    backgroundNotesTests=true;NotesBackgroundEvents events;events.Start();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    Gdiplus::GdiplusStartupInput graphics;Gdiplus::GdiplusStartup(&g_gdiplusToken,&graphics,nullptr);
    g_hInst=GetModuleHandleW(nullptr);g_bThemeIsDark=true;g_selectedBrowser=BrowserKind::Edge;
    // Own the decoder's module independently of the Notes state being tested;
    // reading durable RTF after both windows close must not use an unloaded class.
    const auto decoderModule=LoadLibraryW(L"Msftedit.dll");Check(decoderModule!=nullptr,"Fixture retains its own post-exit rich text decoder module");
    g_sDataDir=fs::current_path()/L"build"/(L"notes-launcher-lifetime-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    fs::create_directories(g_sDataDir);g_sConfigPath=g_sDataDir/L"ctSpaces.ini";
    WritePrivateProfileStringW(L"notes_window",L"help_seen",L"1",g_sConfigPath.c_str());
    CloseAndAutosave();
#ifndef CTSPACES_LIFETIME_BASELINE
    MinimizeAndBusy();TrayFailure();ExplicitExitAndFailure();ExplicitExitDuringLaunch();DefaultCompletionDuringExit();DisabledChooserGuard();
#endif
    NotesBackgroundAudit("final independent lifetime audit");NotesBackgroundBanner();
    std::cout<<assertions<<" lifetime checks; "<<scenarios<<" private scenarios; "<<failures<<" failures; helper checks="<<checks<<std::endl;
    FreeLibrary(decoderModule);
    return failures ? 1 : 0;
  } catch(const std::exception &error) {std::cerr<<"FAIL exception: "<<error.what()<<std::endl;NotesBackgroundBanner();return 1;}
}
