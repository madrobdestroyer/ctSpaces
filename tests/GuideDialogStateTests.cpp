// Native guide state transitions with private INI files and hidden windows.
// The real application entrypoint and presentation/launch APIs are never used.
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
static unsigned guideSuppressed=0,guideForbidden=0,guideShows=0,guideFocus=0,guideForeground=0;
static unsigned guideNotices=0,guideEnds=0;
static HWND inputWindow=nullptr;
static HWND unrelatedInputWindow=nullptr;
static unsigned unrelatedKeys=0;
enum class InputExit {None,Cancel,Accept,Quit,MessageError,OtherReturn,OtherEscape};
static InputExit inputExit=InputExit::None;
static void GuideOwned(HWND window){DWORD pid=0;if(!window||!IsWindow(window)||!GetWindowThreadProcessId(window,&pid)||pid!=GetCurrentProcessId()){++guideForbidden;throw std::runtime_error("Non-owned guide window refused");}}
static HWND WINAPI GuideCreate(DWORD ex,LPCWSTR kind,LPCWSTR text,DWORD style,int x,int y,int width,int height,HWND owner,HMENU menu,HINSTANCE instance,LPVOID data){if(style&WS_VISIBLE)++guideSuppressed;HWND window=::CreateWindowExW((ex&~WS_EX_TOPMOST)|WS_EX_NOACTIVATE,kind,text,style&~WS_VISIBLE,x,y,width,height,owner,menu,instance,data);if(HIWORD(reinterpret_cast<ULONG_PTR>(kind))&&wcscmp(kind,L"InputBoxWndClass")==0)inputWindow=window;return window;}
static BOOL WINAPI GuideGetMessage(LPMSG message,HWND window,UINT first,UINT last){if(inputExit!=InputExit::None){const auto exit=inputExit;inputExit=InputExit::None;if(exit==InputExit::Quit){::PostQuitMessage(73);return ::GetMessageW(message,window,first,last);}if(exit==InputExit::MessageError){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return -1;}if(exit==InputExit::OtherReturn||exit==InputExit::OtherEscape){GuideOwned(unrelatedInputWindow);if(!::PostMessageW(unrelatedInputWindow,WM_KEYDOWN,exit==InputExit::OtherReturn?VK_RETURN:VK_ESCAPE,0))throw std::runtime_error("Private synthetic recipient key could not queue");inputExit=InputExit::Cancel;const BOOL read=::GetMessageW(message,unrelatedInputWindow,WM_KEYDOWN,WM_KEYDOWN);if(read!=TRUE||message->hwnd!=unrelatedInputWindow||message->message!=WM_KEYDOWN)throw std::runtime_error("Synthetic private key retrieval mismatch");std::cout<<"SYNTHETIC GETMESSAGE status="<<read<<" message="<<message->message<<" key="<<message->wParam<<" expected_recipient="<<(message->hwnd==unrelatedInputWindow)<<std::endl;return read;}GuideOwned(inputWindow);if(exit==InputExit::Accept)::SetWindowTextW(::GetDlgItem(inputWindow,102),L"confirmed.example");::SendMessageW(inputWindow,WM_COMMAND,exit==InputExit::Accept?IDOK:IDCANCEL,0);*message={};message->message=WM_NULL;return TRUE;}return ::GetMessageW(message,window,first,last);}
static BOOL WINAPI GuideTranslate(const MSG *message){if(message->hwnd==unrelatedInputWindow&&message->message==WM_KEYDOWN)return FALSE;return ::TranslateMessage(message);}
static BOOL WINAPI GuideNoShow(HWND,int){++guideSuppressed;return FALSE;}
static HWND WINAPI GuideNoFocus(HWND){++guideSuppressed;return nullptr;}
static BOOL WINAPI GuideNoForeground(HWND){++guideSuppressed;return FALSE;}
static BOOL WINAPI GuideNoRaise(HWND){++guideSuppressed;return FALSE;}
static BOOL WINAPI GuidePosition(HWND w,HWND after,int x,int y,int width,int height,UINT flags){GuideOwned(w);if((flags&SWP_SHOWWINDOW)||!(flags&SWP_NOACTIVATE))++guideSuppressed;return ::SetWindowPos(w,after,x,y,width,height,(flags&~SWP_SHOWWINDOW)|SWP_NOACTIVATE); }
static BOOL WINAPI GuideRedraw(HWND w,const RECT *area,HRGN region,UINT flags){GuideOwned(w);return ::RedrawWindow(w,area,region,flags);}
static BOOL WINAPI GuideInvalidate(HWND w,const RECT *area,BOOL erase){GuideOwned(w);return ::InvalidateRect(w,area,erase);}
static BOOL WINAPI GuidePost(HWND w,UINT,WPARAM,LPARAM){GuideOwned(w);return TRUE;}
static BOOL WINAPI GuideEnd(HWND w,INT_PTR){GuideOwned(w);++guideEnds;return ::DestroyWindow(w);}
static BOOL WINAPI GuideClipboard(HWND){++guideForbidden;return FALSE;}
static HINSTANCE WINAPI GuideNoShell(HWND,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,INT){++guideForbidden;return reinterpret_cast<HINSTANCE>(ERROR_ACCESS_DENIED);}
static BOOL WINAPI GuideNoShellEx(SHELLEXECUTEINFOW*){++guideForbidden;return FALSE;}
static BOOL WINAPI GuideNoProcess(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION){++guideForbidden;return FALSE;}
static LSTATUS WINAPI GuideNoSystemValue(HKEY,LPCWSTR,LPCWSTR,LPDWORD,LPVOID,LPDWORD){++guideForbidden;return ERROR_ACCESS_DENIED;}
static HWND WINAPI GuideDialogResource(HINSTANCE,LPCWSTR,HWND,DLGPROC,LPARAM);
#define CreateDialogParamW GuideDialogResource
#define CreateWindowExW GuideCreate
#define GetMessageW GuideGetMessage
#define TranslateMessage GuideTranslate
#define ShowWindow GuideNoShow
#define SetFocus GuideNoFocus
#define SetForegroundWindow GuideNoForeground
#define SetActiveWindow GuideNoFocus
#define BringWindowToTop GuideNoRaise
#define SetWindowPos GuidePosition
#define RedrawWindow GuideRedraw
#define InvalidateRect GuideInvalidate
#define PostMessageW GuidePost
#define EndDialog GuideEnd
#define OpenClipboard GuideClipboard
#define ShellExecuteW GuideNoShell
#define ShellExecuteExW GuideNoShellEx
#define CreateProcessW GuideNoProcess
#define SHGetValueW GuideNoSystemValue
#define CTSPACES_INSTALLER_TEST_HOOKS
#define wWinMain UnusedGuideApplicationEntry
#ifdef CTSPACES_GUIDE_DIALOG_BEFORE
#include "../build/app-audit-round9-2026-10-15/guide-settings/ctSpaces-before.cpp"
#else
#include "../ctSpaces.cpp"
#endif
#undef wWinMain
#undef EndDialog

int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR text,LPCWSTR,UINT){++guideNotices;std::wcout<<L"Notice: "<<text<<L'\n';return IDOK;}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path&,std::wstring&){return false;}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path&){}

static unsigned guideChecks=0,guideFailures=0,guideCases=0;
static void Check(bool value,const char *message){++guideChecks;if(!value)++guideFailures;std::cout<<(value?"PASS ":"FAIL ")<<message<<'\n';}
static std::string Bytes(){std::ifstream file(g_sConfigPath,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
static void CALLBACK GuideEvent(HWINEVENTHOOK,DWORD event,HWND window,LONG object,LONG,DWORD,DWORD){DWORD pid=0;if(!window||!GetWindowThreadProcessId(window,&pid)||pid!=GetCurrentProcessId())return;if(event==EVENT_OBJECT_SHOW&&object==OBJID_WINDOW)++guideShows;else if(event==EVENT_OBJECT_FOCUS)++guideFocus;else if(event==EVENT_SYSTEM_FOREGROUND)++guideForeground;}
static void Audit(){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if((msg.message>=WM_KEYFIRST&&msg.message<=WM_KEYLAST)||(msg.message>=WM_MOUSEFIRST&&msg.message<=WM_MOUSELAST))throw std::runtime_error("Device input excluded");if(msg.message!=WM_QUIT)DispatchMessageW(&msg);}unsigned visible=0;EnumWindows([](HWND w,LPARAM value)->BOOL{DWORD pid=0;if(GetWindowThreadProcessId(w,&pid)&&pid==GetCurrentProcessId()&&IsWindowVisible(w))++*reinterpret_cast<unsigned*>(value);return TRUE;},reinterpret_cast<LPARAM>(&visible));GUITHREADINFO state{sizeof(state)};if(!GetGUIThreadInfo(GetCurrentThreadId(),&state)||visible||state.hwndFocus||state.hwndActive||state.hwndCapture||guideShows||guideFocus||guideForeground||guideForbidden)throw std::runtime_error("Background guide safety check failed");}
static INT_PTR CALLBACK HiddenGuideProc(HWND w,UINT m,WPARAM p,LPARAM l){if(m==WM_INITDIALOG)return FALSE;return GuideDlgProc(w,m,p,l);}
static INT_PTR CALLBACK HiddenTourProc(HWND w,UINT m,WPARAM p,LPARAM l){if(m==WM_INITDIALOG)return FALSE;return QuickTourDlgProc(w,m,p,l);}
static HWND MakeDialog(HWND owner,DLGPROC proc){struct {DLGTEMPLATE dialog;WORD menu=0,kind=0,title=0;} model{};model.dialog.style=WS_POPUP|DS_MODALFRAME;model.dialog.dwExtendedStyle=WS_EX_NOACTIVATE;model.dialog.cx=570;model.dialog.cy=365;HWND window=::CreateDialogIndirectParamW(g_hInst,&model.dialog,owner,proc,0);GuideOwned(window);return window;}
static HWND Control(HWND parent,int id,LPCWSTR kind,DWORD style=0){HWND w=GuideCreate(0,kind,L"",WS_CHILD|style,0,0,50,20,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),g_hInst,nullptr);GuideOwned(w);return w;}
static HWND OpenGuide(GuideDialogState &state){HWND window=MakeDialog(g_hGui,HiddenGuideProc);Control(window,IDC_GUIDE_TOPICS,L"LISTBOX",LBS_HASSTRINGS|LBS_NOTIFY);Control(window,IDC_GUIDE_BODY,L"EDIT",ES_MULTILINE|ES_READONLY);for(int id:{IDC_GUIDE_TOPICS_LABEL,IDC_GUIDE_TITLE,IDC_GUIDE_LOCATION,IDC_GUIDE_COUNT})Control(window,id,L"STATIC");for(int id:{IDOK,IDCANCEL,IDC_GUIDE_BACK,IDC_GUIDE_QUICK_TOUR})Control(window,id,L"BUTTON");GuideDlgProc(window,WM_INITDIALOG,0,reinterpret_cast<LPARAM>(&state));Audit();return window;}
static HWND WINAPI GuideDialogResource(HINSTANCE,LPCWSTR resource,HWND owner,DLGPROC proc,LPARAM data){if(resource!=MAKEINTRESOURCEW(IDD_QUICK_TOUR)||proc!=QuickTourDlgProc){++guideForbidden;return nullptr;}HWND window=MakeDialog(owner,HiddenTourProc);for(int id:{IDC_QUICK_TOUR_TITLE,IDC_QUICK_TOUR_BODY,IDC_QUICK_TOUR_COUNT,IDC_QUICK_TOUR_DEMO})Control(window,id,L"STATIC");for(int id:{IDOK,IDCANCEL,IDC_QUICK_TOUR_BACK})Control(window,id,L"BUTTON");QuickTourDlgProc(window,WM_INITDIALOG,0,data);return window;}
static void Seed(const fs::path &root,bool welcomeHandled=false){++guideCases;g_sDataDir=root/std::to_wstring(guideCases);fs::create_directory(g_sDataDir);g_sConfigPath=g_sDataDir/L"config.ini";std::wstring detail;std::vector<config_persistence::IniMutation> initial{{L"sentinel",L"preserve",L"private unrelated settings"}};auto state=welcomeHandled?guided_walkthrough::WelcomeHandledMutations():guided_walkthrough::WelcomePendingMutations();initial.insert(initial.end(),state.begin(),state.end());if(!config_persistence::ApplyIniMutationsAtomically(g_sConfigPath,initial,&detail))throw std::runtime_error("Private guide seed failed");g_guideState=guided_walkthrough::LoadState(g_sConfigPath);g_bUiEnabled=true;g_bDefaultProfileUiBusy=false;g_isLaunchInFlight=false;g_bArchiveTaskInProgress=false;g_isShuttingDown=false;g_bCleanupBusy=false;g_bClientNotesBusy=false;g_bCloseAllClientsPending=false;EnableWindow(g_hGui,TRUE);}
static HANDLE Lock(){HANDLE lock=::CreateFileW(g_sConfigPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("Native private guide settings lock failed");return lock;}
static void Command(HWND dialog,int command){SendMessageW(dialog,WM_COMMAND,command,0);Audit();}
static bool PendingOnDisk(){auto saved=guided_walkthrough::LoadState(g_sConfigPath);return saved.welcomePending&&!saved.welcomeHandled;}
static LRESULT CALLBACK OtherInputProc(HWND window,UINT message,WPARAM w,LPARAM l){return DefWindowProcW(window,message,w,l);}
static LRESULT CALLBACK OtherEditProc(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){if(message==WM_KEYDOWN)++unrelatedKeys;if(message==WM_NCDESTROY)RemoveWindowSubclass(window,OtherEditProc,1);return DefSubclassProc(window,message,w,l);}

int wmain(int argc,wchar_t**){std::vector<HWINEVENTHOOK> hooks;try{
  if(argc!=1)throw std::runtime_error("Unknown guide-state argument refused before initialization");
  for(const DWORD event:{EVENT_OBJECT_SHOW,EVENT_OBJECT_FOCUS,EVENT_SYSTEM_FOREGROUND}){auto hook=SetWinEventHook(event,event,nullptr,GuideEvent,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);if(!hook)throw std::runtime_error("Native guide observer unavailable");hooks.push_back(hook);}
  g_hInst=GetModuleHandleW(nullptr);g_bQaInstance=true;g_sExeDir=GetCurrentExecutablePath().parent_path();INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};if(!InitCommonControlsEx(&controls))throw std::runtime_error("Control initialization failed");Gdiplus::GdiplusStartupInput graphics;Gdiplus::GdiplusStartup(&g_gdiplusToken,&graphics,nullptr);LoadBakedThemesFromHeader();BuildThemeColors(true,g_themeColors);g_bThemeIsDark=true;
  GUID id{};wchar_t identity[40]{};if(FAILED(CoCreateGuid(&id))||!StringFromGUID2(id,identity,40))throw std::runtime_error("Unique guide fixture identity failed");const auto root=fs::current_path()/L"build"/L"app-audit-round9-2026-10-15"/L"guide-settings"/identity;fs::create_directories(root.parent_path());if(!fs::create_directory(root))throw std::runtime_error("Unique private guide fixture could not be created");
  g_hGui=GuideCreate(WS_EX_NOACTIVATE,L"STATIC",L"Hidden guide state fixture",WS_OVERLAPPED,0,0,600,240,nullptr,nullptr,g_hInst,nullptr);GuideOwned(g_hGui);
  // Reading and selecting content must not acknowledge anything without Next.
  Seed(root);GuideDialogState state;state.initialWelcome=true;state.visibleTopics=guided_walkthrough::AllTopicIndices();HWND dialog=OpenGuide(state);const auto initial=Bytes();
  SendDlgItemMessageW(dialog,IDC_GUIDE_TOPICS,LB_SETCURSEL,3,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_GUIDE_TOPICS,LBN_SELCHANGE),0);Audit();
  Check(state.position==3&&Bytes()==initial&&PendingOnDisk()&&guided_walkthrough::IsUnread(g_guideState,3),"Selecting a guide topic changes only the displayed page; unread and welcome state remain untouched");Command(dialog,IDC_GUIDE_BACK);Check(state.position==2&&Bytes()==initial,"Back navigation leaves persisted welcome and read state untouched");
  SendMessageW(dialog,WM_APP_DISMISS_GUIDE,0,0);Audit();Check(!IsWindow(dialog)&&state.externallyDismissed&&!state.stateChanged&&Bytes()==initial&&PendingOnDisk(),"External client-launch dismissal closes guide without acknowledging welcome or marking topics read");
  // An actual locked INI rejects Next, preserves page and permits exact retry.
  Seed(root);state={};state.initialWelcome=true;state.visibleTopics={0,1};dialog=OpenGuide(state);const auto nextBytes=Bytes();HANDLE lock=Lock();unsigned notices=guideNotices;Command(dialog,IDOK);
  Check(IsWindow(dialog)&&state.position==0&&!state.stateChanged&&!g_guideState.welcomeHandled&&guided_walkthrough::IsUnread(g_guideState,0),"Failed Next retains the same guide page and in-memory unread welcome state");Check(Bytes()==nextBytes&&PendingOnDisk()&&guideNotices==notices+1,"Failed Next reports one warning and preserves exact saved settings");CloseHandle(lock);Command(dialog,IDOK);
  auto saved=guided_walkthrough::LoadState(g_sConfigPath);Check(IsWindow(dialog)&&state.position==1&&state.stateChanged&&saved.welcomeHandled&&!saved.welcomePending&&!guided_walkthrough::IsUnread(saved,0)&&guided_walkthrough::IsUnread(saved,1),"Unlock and retry commits only the displayed topic and then advances to the next page");
  lock=Lock();const auto doneBytes=Bytes();notices=guideNotices;Command(dialog,IDOK);Check(IsWindow(dialog)&&state.position==1&&Bytes()==doneBytes&&guideNotices==notices+1&&guided_walkthrough::IsUnread(g_guideState,1),"Failed Done keeps final unread topic available without closing or changing settings");CloseHandle(lock);Command(dialog,IDOK);saved=guided_walkthrough::LoadState(g_sConfigPath);Check(!IsWindow(dialog)&&!guided_walkthrough::IsUnread(saved,1),"Final-page retry commits its read revision before closing");
  // Initial-tour handoff must not erase welcome acknowledgement on failure.
  Seed(root);state={};state.initialWelcome=true;state.visibleTopics={0};dialog=OpenGuide(state);lock=Lock();notices=guideNotices;const auto tourBytes=Bytes();Command(dialog,IDC_GUIDE_QUICK_TOUR);Check(IsWindow(dialog)&&!state.launchQuickTour&&Bytes()==tourBytes&&guideNotices==notices+1&&PendingOnDisk(),"Failed initial Quick tour handoff stays open and preserves pending acknowledgement");CloseHandle(lock);Command(dialog,IDC_GUIDE_QUICK_TOUR);saved=guided_walkthrough::LoadState(g_sConfigPath);Check(!IsWindow(dialog)&&state.launchQuickTour&&saved.welcomeHandled&&!saved.welcomePending&&guided_walkthrough::IsUnread(saved,0),"Retrying initial Quick tour saves welcome acknowledgement without marking an unvisited topic read");
  // Skip/Close is intentionally dismissible after warning, and remains pending.
  for(const bool closeMessage:{false,true}){Seed(root);state={};state.initialWelcome=true;state.visibleTopics={0};dialog=OpenGuide(state);lock=Lock();const auto skipBytes=Bytes();notices=guideNotices;if(closeMessage)SendMessageW(dialog,WM_CLOSE,0,0);else Command(dialog,IDCANCEL);CloseHandle(lock);Audit();Check(!IsWindow(dialog)&&!state.stateChanged&&Bytes()==skipBytes&&PendingOnDisk()&&guideNotices==notices+1,closeMessage?"Close after a save warning dismisses safely and keeps welcome pending for a later run":"Skip after a save warning dismisses safely and keeps welcome pending for a later run");}
  Seed(root,true);state={};state.visibleTopics=guided_walkthrough::UnreadAnnouncementIndices(g_guideState);state.whatsNewOnly=true;dialog=OpenGuide(state);lock=Lock();const auto readOnlyBytes=Bytes();notices=guideNotices;Command(dialog,IDCANCEL);CloseHandle(lock);Check(!IsWindow(dialog)&&Bytes()==readOnlyBytes&&guideNotices==notices,"Closing What's new with welcome already handled makes no write, even when settings are locked");
  // The actual modeless-tour procedures may position hidden private windows;
  // all presentation, input, process and external side-effect boundaries are inert.
  Seed(root,true);const auto beforeTour=Bytes();ShowQuickTour();Audit();HWND tour=g_hQuickTourDialog;Check(tour&&IsWindow(tour)&&g_hQuickTourFrame&&IsWindow(g_hQuickTourFrame)&&!IsWindowEnabled(g_hGui),"Quick tour creates its hidden owned overlay and disables the private launcher");
  for(size_t pos=0;pos<kQuickTourSteps.size();++pos){Check(g_quickTourState.position==pos&&IsWindow(tour),"Quick tour visits the expected real production step");if(pos+1<kQuickTourSteps.size())Command(tour,IDOK);}
  Command(tour,IDC_QUICK_TOUR_BACK);Check(g_quickTourState.position==kQuickTourSteps.size()-2,"Quick tour Back returns to the prior step");Command(tour,IDOK);Command(tour,IDOK);Check(!g_hQuickTourDialog&&!g_hQuickTourFrame&&!IsWindow(tour)&&IsWindowEnabled(g_hGui)&&Bytes()==beforeTour,"Quick tour Done destroys both overlays, re-enables its idle owner and writes no configuration");
  for(int busy=0;busy<5;++busy){Seed(root,true);ShowQuickTour();Audit();tour=g_hQuickTourDialog;Check(tour&&IsWindow(tour)&&!IsWindowEnabled(g_hGui),"Repeated tour begins with an owned disabled launcher");if(busy==0)g_bDefaultProfileUiBusy=true;if(busy==1)g_isLaunchInFlight=true;if(busy==2)g_bArchiveTaskInProgress=true;if(busy==3)g_isShuttingDown=true;if(busy==4)g_bUiEnabled=false;DismissQuickTour(false);Audit();Check(!g_hQuickTourDialog&&!g_hQuickTourFrame&&!IsWindowEnabled(g_hGui),"Tour cleanup never re-enables a main window whose operation or shutdown still owns its disabled state");}
  Seed(root,true);ShowQuickTour();Audit();DismissQuickTourForOwnerClosing();Audit();Check(!g_hQuickTourDialog&&!g_hQuickTourFrame&&!IsWindowEnabled(g_hGui),"Owner-closing dismissal destroys overlays without resurrecting the closing launcher");
  Seed(root,true);for(int cycle=0;cycle<4;++cycle){ShowQuickTour();Audit();tour=g_hQuickTourDialog;Command(tour,cycle%2?IDCANCEL:IDOK);if(g_hQuickTourDialog)DismissQuickTour(false);Audit();Check(!g_hQuickTourDialog&&!g_hQuickTourFrame&&IsWindowEnabled(g_hGui)&&!g_quickTourState.font&&!g_quickTourState.strongFont,"Repeated hidden tour opening and dismissal releases window and font state");}
  // Native shared input windows must never outlive their stack-local InputParams.
  // No device messages are used: modal exit boundaries deliver an owned command,
  // an actual queued WM_QUIT, or an explicit GetMessage failure respectively.
  for(const auto exit:{InputExit::Cancel,InputExit::Accept,InputExit::Quit,InputExit::MessageError}){Seed(root,true);inputWindow=nullptr;inputExit=exit;const auto text=InputBoxWindow::Show(g_hGui,L"Private input lifecycle",L"Private website domain",L"original.example");
    const bool alive=inputWindow&&IsWindow(inputWindow);const bool stale=alive&&GetWindowLongPtrW(inputWindow,GWLP_USERDATA)!=0;
    std::cout<<"INPUT EXIT="<<static_cast<int>(exit)<<" alive="<<alive<<" retained_stack_state="<<stale<<" confirmed="<<text.has_value()<<'\n';
    Check(!alive&&!stale,"Shared input window is destroyed before its stack-local modal state leaves scope");
    Check(exit==InputExit::Accept?(text&&*text==L"confirmed.example"):!text,"Shared input modal result preserves confirmation and cancels interrupted exits");
    MSG quit{};const bool hasQuit=PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)!=FALSE;Check(exit==InputExit::Quit?(hasQuit&&quit.wParam==73):!hasQuit,"Input modal cleanup preserves an existing quit code without inventing a quit request");
    // An unrepaired before build retains a dangling state pointer: clear it
    // before cleanup so this fixture proves lifetime failure without using it.
    if(alive){SetWindowLongPtrW(inputWindow,GWLP_USERDATA,0);DestroyWindow(inputWindow);}inputWindow=nullptr;Audit();Check(IsWindowEnabled(g_hGui),"Input modal exit restores its previously enabled private owner");
  }
  // Explicitly queued synthetic keys target a separate private EDIT control.
  // Translation is suppressed for these exact messages; no device is read.
  WNDCLASSW otherClass{};otherClass.lpfnWndProc=OtherInputProc;otherClass.hInstance=g_hInst;otherClass.lpszClassName=L"CtSpacesPrivateGuideOtherInput";if(!RegisterClassW(&otherClass))throw std::runtime_error("Private unrelated recipient class failed");
  for(const auto exit:{InputExit::OtherReturn,InputExit::OtherEscape}){Seed(root,true);HWND otherOwner=GuideCreate(WS_EX_NOACTIVATE,otherClass.lpszClassName,L"Private separate modeless editor",WS_POPUP,0,0,200,100,g_hGui,nullptr,g_hInst,nullptr);GuideOwned(otherOwner);unrelatedInputWindow=Control(otherOwner,8001,L"EDIT",ES_MULTILINE);SetWindowSubclass(unrelatedInputWindow,OtherEditProc,1,0);unrelatedKeys=0;inputWindow=nullptr;inputExit=exit;const auto text=InputBoxWindow::Show(g_hGui,L"Private input routing",L"Private website domain",L"original.example");
    std::cout<<"OTHER INPUT="<<static_cast<int>(exit)<<" confirmed="<<text.has_value()<<" delivered_to_recipient="<<unrelatedKeys<<'\n';
    Check(!text,"A key addressed to another private window never confirms the shared input dialog");Check(unrelatedKeys==1,"A separate window receives its own Return or Escape instead of the modal input consuming it");Check(inputWindow&&!IsWindow(inputWindow),"Unrelated input routing and subsequent owned Cancel release the input window");inputExit=InputExit::None;DestroyWindow(otherOwner);unrelatedInputWindow=nullptr;inputWindow=nullptr;Audit();
  }
  UnregisterClassW(otherClass.lpszClassName,g_hInst);
  DestroyWindow(g_hGui);g_hGui=nullptr;Audit();for(const auto hook:hooks)UnhookWinEvent(hook);hooks.clear();Gdiplus::GdiplusShutdown(g_gdiplusToken);
  std::cout<<"Guide dialog state: "<<guideChecks<<" checks, "<<guideCases<<" scenarios, "<<guideFailures<<" failures; suppressed presentation="<<guideSuppressed<<"; native show/focus/foreground="<<guideShows<<'/'<<guideFocus<<'/'<<guideForeground<<"; forbidden="<<guideForbidden<<".\n";std::wcout<<L"FIXTURE="<<root<<L'\n';return guideFailures?1:0;
}catch(const std::exception &error){std::cerr<<"Guide-state safety/setup failure: "<<error.what()<<'\n';for(const auto hook:hooks)UnhookWinEvent(hook);return 2;}}

