// Real hidden EDIT/COMBO controls exercise launcher input rollback. No device
// input, foreground windows, real clipboard, tray or browser launch is allowed.
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
static unsigned presentationRequests=0,launchRequests=0,forbiddenTargets=0;
static unsigned suppressedPopups=0,clipboardRequests=0,nativeShows=0,nativeFocus=0,nativeForeground=0;
static bool captureTextResult=false;
static int capturedTextResult=-1;
static void RequireOwned(HWND window){DWORD pid=0;if(!window||!IsWindow(window)||!GetWindowThreadProcessId(window,&pid)||pid!=GetCurrentProcessId()){++forbiddenTargets;throw std::runtime_error("Refused missing or foreign window");}}
static BOOL WINAPI NoShow(HWND,int){++presentationRequests;return FALSE;}
static HWND WINAPI NoFocus(HWND){++presentationRequests;return nullptr;}
static BOOL WINAPI NoForeground(HWND){++presentationRequests;return FALSE;}
static HWND WINAPI NoActive(HWND){++presentationRequests;return nullptr;}
[[maybe_unused]] static BOOL WINAPI NoRaise(HWND){++presentationRequests;return FALSE;}
static HWND WINAPI HiddenCreate(DWORD extended,LPCWSTR kind,LPCWSTR text,DWORD style,int x,int y,int width,int height,HWND parent,HMENU menu,HINSTANCE instance,LPVOID data){return ::CreateWindowExW(extended|WS_EX_NOACTIVATE,kind,text,style&~WS_VISIBLE,x,y,width,height,parent,menu,instance,data);}
static BOOL WINAPI SafePosition(HWND window,HWND after,int x,int y,int width,int height,UINT flags){RequireOwned(window);if((flags&SWP_SHOWWINDOW)||!(flags&SWP_NOACTIVATE)){++presentationRequests;return FALSE;}return ::SetWindowPos(window,after,x,y,width,height,flags);}
static BOOL WINAPI SafeInvalidate(HWND window,const RECT *area,BOOL erase){RequireOwned(window);return ::InvalidateRect(window,area,erase);}
static BOOL WINAPI SafeRedraw(HWND window,const RECT *area,HRGN region,UINT flags){RequireOwned(window);return ::RedrawWindow(window,area,region,flags);}
static BOOL WINAPI SafePost(HWND window,UINT,WPARAM,LPARAM){RequireOwned(window);return TRUE;}
static BOOL WINAPI TrackSetText(HWND window,LPCWSTR text){RequireOwned(window);const BOOL result=::SetWindowTextW(window,text);if(captureTextResult)capturedTextResult=result;return result;}
static LRESULT WINAPI SafeSend(HWND window,UINT message,WPARAM w,LPARAM l){
  if((message==CB_SHOWDROPDOWN&&w)||(message==TTM_TRACKACTIVATE&&w)){++suppressedPopups;return 0;}
  if(message==WM_COPY||message==WM_CUT||message==WM_PASTE){++clipboardRequests;throw std::runtime_error("Clipboard message forbidden");}
  return ::SendMessageW(window,message,w,l);
}
static HANDLE WINAPI NoProcess(DWORD,BOOL,DWORD){++launchRequests;SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
static HINSTANCE WINAPI NoShell(HWND,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,INT){++launchRequests;return reinterpret_cast<HINSTANCE>(ERROR_ACCESS_DENIED);}
static BOOL WINAPI NoShellEx(SHELLEXECUTEINFOW *){++launchRequests;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static BOOL WINAPI NoCreateProcess(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION){++launchRequests;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static BOOL WINAPI NoTray(DWORD,PNOTIFYICONDATAW){++launchRequests;return FALSE;}
static BOOL WINAPI NoCursor(LPPOINT point){if(point)*point={};return FALSE;}
static BOOL WINAPI NoRelease(){++presentationRequests;return FALSE;}
static HWND WINAPI NoCapture(HWND){++presentationRequests;return nullptr;}
static BOOL WINAPI NoClipboard(HWND){++clipboardRequests;return FALSE;}
#define ShowWindow NoShow
#define SetFocus NoFocus
#define SetForegroundWindow NoForeground
#define SetActiveWindow NoActive
#define BringWindowToTop NoRaise
#define CreateWindowExW HiddenCreate
#define SetWindowPos SafePosition
#define InvalidateRect SafeInvalidate
#define RedrawWindow SafeRedraw
#define PostMessageW SafePost
#define SendMessageW SafeSend
#define SetWindowTextW TrackSetText
#define OpenProcess NoProcess
#define ShellExecuteW NoShell
#define ShellExecuteExW NoShellEx
#define CreateProcessW NoCreateProcess
#define Shell_NotifyIconW NoTray
#define GetCursorPos NoCursor
#define SetCapture NoCapture
#define ReleaseCapture NoRelease
#define OpenClipboard NoClipboard
#define CTSPACES_INSTALLER_TEST_HOOKS
#define wWinMain ProductionEntryNotRun
#include "../ctSpaces.cpp"
#undef wWinMain
#undef PostMessageW

static unsigned notices=0,checks=0,failures=0,nativeChanges=0,scenarios=0;
static std::wstring lastNotice;
int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR message,LPCWSTR,UINT){++notices;lastNotice=message?message:L"";return IDOK;}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path &,std::wstring &){return false;}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path &){}
static void Check(bool condition,const char *message){++checks;if(!condition)++failures;std::cout<<(condition?"PASS ":"FAIL ")<<message<<'\n';}
static void CALLBACK Observe(HWINEVENTHOOK,DWORD event,HWND window,LONG object,LONG,DWORD,DWORD){DWORD pid=0;if(!window||!GetWindowThreadProcessId(window,&pid)||pid!=GetCurrentProcessId())return;if(event==EVENT_OBJECT_SHOW&&object==OBJID_WINDOW&&IsWindowVisible(GetAncestor(window,GA_ROOT)))++nativeShows;if(event==EVENT_OBJECT_FOCUS)++nativeFocus;if(event==EVENT_SYSTEM_FOREGROUND)++nativeForeground;}
static LRESULT CALLBACK HiddenParent(HWND window,UINT message,WPARAM w,LPARAM l){
  if(message==WM_COMMAND&&HIWORD(w)==EN_CHANGE&&reinterpret_cast<HWND>(l)==g_hClientEdit){++nativeChanges;HandleClientInputChanged();return 0;}
  return DefWindowProcW(window,message,w,l);
}
static bool rejectNativeText=false;
static unsigned rejectedNativeTextMessages=0;
static LRESULT CALLBACK RejectTextSubclass(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
  if(message==WM_SETTEXT&&rejectNativeText){++rejectedNativeTextMessages;return FALSE;}
  return DefSubclassProc(window,message,w,l);
}
static void NativeReplace(const std::wstring &value){
  RequireOwned(g_hClientEdit);const auto before=nativeChanges;
  SendMessageW(g_hClientEdit,EM_SETSEL,0,-1);SendMessageW(g_hClientEdit,EM_REPLACESEL,FALSE,reinterpret_cast<LPARAM>(value.c_str()));
  Check(nativeChanges>before,"A real hidden EDIT dispatches EN_CHANGE to the production input handler");
}
static void Seed(const std::wstring &value){
  g_bSyncingClientInput=true;SetWindowTextW(g_hClientEdit,L"");g_bSyncingClientInput=false;
  g_sLastValidComboText.clear();g_bFilteringClientList=false;g_iSelectedSessionTab=0;
  if(!value.empty())NativeReplace(value);
}
static void RejectAfterProgrammaticSelection(const std::wstring &typed,const std::wstring &selected,const wchar_t *invalid,const char *message){
  ++scenarios;Seed(typed);SetClientInputText(selected);
  Check(GetClientInputText()==selected,"Programmatic client selection reaches the native EDIT");
  NativeReplace(selected+invalid);Check(GetClientInputText()==selected&&g_sLastValidComboText==selected,message);
  DWORD start=0,end=0;SendMessageW(g_hClientEdit,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
  Check(start==selected.size()&&end==selected.size(),"Rejected invalid input leaves the caret at the retained client's end");
}
static void InputRollbackCases(){
  RejectAfterProgrammaticSelection(L"Alpha",L"Bravo",L"/","Invalid slash after selecting Bravo must retain Bravo instead of stale Alpha");
  RejectAfterProgrammaticSelection(L"",L"Bravo",L":","Invalid colon after the first programmatic selection must retain Bravo");
  RejectAfterProgrammaticSelection(L"Alpha",L"\u65e5\u672c \U0001f680 Office",L"/","Invalid paste after Unicode programmatic selection must preserve its exact name");
  RejectAfterProgrammaticSelection(L"Alpha",L"",L":","Clearing client input programmatically must not resurrect the previous client");
  ++scenarios;Seed(L"Alpha");g_bSyncingClientInput=true;SetWindowTextW(g_hClientEdit,L"Bravo");g_bSyncingClientInput=false;
  SetClientInputText(L"Bravo");NativeReplace(L"Bravo/");
  Check(GetClientInputText()==L"Bravo"&&g_sLastValidComboText==L"Bravo","Same-text programmatic synchronization refreshes the valid rollback baseline");
  ++scenarios;Seed(L"Alpha");SendMessageW(g_hComboClient,CB_RESETCONTENT,0,0);SendMessageW(g_hComboClient,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Bravo"));SendMessageW(g_hComboClient,CB_SETCURSEL,0,0);
  Check(SyncClientInputFromComboSelection(true),"The real combo selection synchronizes into the overlaid EDIT");
  NativeReplace(L"Bravo:");Check(GetClientInputText()==L"Bravo","Invalid input after dropdown selection retains the dropdown client");
  ++scenarios;Seed(L"Alpha");NativeReplace(L"Bravo");NativeReplace(L"Bravo/");
  Check(GetClientInputText()==L"Bravo","Normal valid typing followed by invalid input retains the latest typed client");
  ++scenarios;Seed(L"Alpha");NativeReplace(L"");NativeReplace(L"/");
  Check(GetClientInputText().empty()&&g_sLastValidComboText.empty(),"Native user clearing followed by invalid input stays empty");
  ++scenarios;Seed(L"Alpha");
  if(!SetWindowSubclass(g_hClientEdit,RejectTextSubclass,0x633031,0))throw std::runtime_error("Cannot install native text-write failure fixture");
  rejectNativeText=true;captureTextResult=true;SetClientInputText(L"Bravo");captureTextResult=false;rejectNativeText=false;RemoveWindowSubclass(g_hClientEdit,RejectTextSubclass,0x633031);
  std::wcout<<L"Native text-rejection diagnostic: requested=Bravo; SetWindowTextW="<<capturedTextResult<<L"; rejected WM_SETTEXT="<<rejectedNativeTextMessages<<L"; actual="<<GetClientInputText()<<L"; baseline="<<g_sLastValidComboText<<L'\n';
  Check(GetClientInputText()==L"Alpha"&&g_sLastValidComboText==L"Alpha","A rejected native programmatic text write preserves the displayed client and rollback baseline");
  NativeReplace(L"Alpha/");Check(GetClientInputText()==L"Alpha","Invalid input after rejected native selection cannot restore the unwritten client");
  ++scenarios;Seed(L"Alpha");SetClientInputText(L"A/B");
  Check(g_sLastValidComboText==L"Alpha","A malformed programmatic value cannot replace the valid rollback baseline");
  NativeReplace(L"A/B:");Check(GetClientInputText()==L"Alpha","Malformed programmatic input rolls back to the last actual valid client");
}
static void ShortcutIdentityCases(){
  ++scenarios;g_bUiEnabled=false;
  const std::vector<std::wstring> malformed={L"A/B",L"A\\B",L"A:B",L"A?B",L"A*B",L"A\"B",L"A<B",L"A>B",L"A|B"};
  for(const auto &name:malformed){
    lastNotice.clear();const auto noticesBefore=notices;
    const bool launched=ProcessClientLaunchRequest(L"--client "+QuoteCommandLineArgument(name),true);
    Check(!launched&&notices==noticesBefore+1&&lastNotice.find(L"no longer matches a profile")!=std::wstring::npos,"Malformed shortcut identity rejects before resolving the unrelated existing AB profile");
  }
  for(const auto *valid:{L"AB",L"ab"}){
    lastNotice.clear();Check(!ProcessClientLaunchRequest(L"--client "+QuoteCommandLineArgument(valid),true)&&lastNotice.find(L"finishing another profile action")!=std::wstring::npos,"Exact and case-insensitive valid shortcut clients still reach the busy gate");
  }
  lastNotice.clear();Check(!ProcessClientLaunchRequest(L"--client Missing",true)&&lastNotice.find(L"no longer matches a profile")!=std::wstring::npos,"A genuinely missing client remains a rejected shortcut");
  g_bUiEnabled=true;
}
static void ProfileSwitchCases(){
  const std::wstring one=L"C:\\private fixture\\one",two=L"C:\\private fixture\\two";
  for(const auto browser:{BrowserKind::Edge,BrowserKind::Chrome,BrowserKind::Brave,BrowserKind::Firefox}){
    ++scenarios;const std::wstring option=browser==BrowserKind::Firefox?L"--profile":L"--user-data-dir";
    const std::wstring split=option+L" "+QuoteCommandLineArgument(one),equals=option+L"="+QuoteCommandLineArgument(one);
    for(const auto &form:{split,equals}){
      const auto result=ExtractBrowserProfileArgument(L"browser.exe "+form,browser);
      Check(result.present&&result.valid&&result.path==one,"A single absolute profile switch retains its exact native-quoted path");
    }
    for(const auto &suffix:{split,equals,option+L" "+QuoteCommandLineArgument(two),option+L"="+QuoteCommandLineArgument(two),option+L"=",option})for(const auto &first:{split,equals}){
      const auto result=ExtractBrowserProfileArgument(L"browser.exe "+first+L" "+suffix,browser);
      Check(result.present&&!result.valid,"Repeated identical, conflicting, empty or incomplete browser profile switches fail closed");
    }
    for(const auto &tail:{option+L" relative-path",option+L"=relative-path",option,option+L"=",option+L" \"\""}){
      const auto result=ExtractBrowserProfileArgument(L"browser.exe "+tail,browser);
      Check(result.present&&!result.valid,"A relative, empty or incomplete single profile switch remains invalid");
    }
    const auto absent=ExtractBrowserProfileArgument(L"browser.exe --profile-directory=Default https://example.test",browser);
    Check(!absent.present&&absent.valid,"Ordinary browser commands without a profile-root switch remain valid");
  }
  for(const auto &tail:{L"-profile \"C:\\private fixture\\one\" --profile=\"C:\\private fixture\\two\"",L"--profile=\"C:\\private fixture\\one\" -profile \"C:\\private fixture\\one\"",L"--USER-DATA-DIR=C:\\one --user-data-dir=C:\\two"}){
    const auto browser=tail[2]==L'U'?BrowserKind::Chrome:BrowserKind::Firefox;
    const auto result=ExtractBrowserProfileArgument(L"browser.exe "+std::wstring(tail),browser);
    Check(result.present&&!result.valid,"Case variants and Firefox short/long profile aliases cannot hide duplicate roots");
  }
}
int wmain(){
  std::vector<HWINEVENTHOOK> hooks;
  try{
    for(const DWORD event:{EVENT_SYSTEM_FOREGROUND,EVENT_OBJECT_SHOW,EVENT_OBJECT_FOCUS}){const auto hook=SetWinEventHook(event,event,nullptr,Observe,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);if(!hook)throw std::runtime_error("Native event observer unavailable");hooks.push_back(hook);}
    g_hInst=GetModuleHandleW(nullptr);g_bQaInstance=true;g_sExeDir=GetCurrentExecutablePath().parent_path();
    g_sDataDir=fs::current_path()/L"build"/L"app-quality-2026-10-19"/L"clients"/(L"fixture-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    if(!fs::create_directories(g_sDataDir/L"Sites"/L"AB"))throw std::runtime_error("Fresh private fixture unavailable");g_sConfigPath=g_sDataDir/L"config.ini";
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_TAB_CLASSES};if(!InitCommonControlsEx(&controls))throw std::runtime_error("Cannot initialize controls");
    Gdiplus::GdiplusStartupInput graphics;if(Gdiplus::GdiplusStartup(&g_gdiplusToken,&graphics,nullptr)!=Gdiplus::Ok)throw std::runtime_error("Graphics initialization failed");
    WNDCLASSW wc{};wc.lpfnWndProc=HiddenParent;wc.hInstance=g_hInst;wc.lpszClassName=L"ctSpaces.HiddenLauncherValidation";if(!RegisterClassW(&wc))throw std::runtime_error("Hidden class registration failed");
    g_hGui=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"Hidden launcher input fixture",WS_OVERLAPPED|WS_CLIPCHILDREN,0,0,600,234,nullptr,nullptr,g_hInst,nullptr);RequireOwned(g_hGui);
    const auto child=[&](const wchar_t *kind,DWORD style){HWND result=CreateWindowExW(WS_EX_NOACTIVATE,kind,L"",WS_CHILD|style,0,0,100,24,g_hGui,nullptr,g_hInst,nullptr);RequireOwned(result);return result;};
    g_hClientEdit=child(L"EDIT",ES_AUTOHSCROLL);g_hComboClient=child(L"COMBOBOX",CBS_DROPDOWN);
    g_hClientEditSurface=child(L"STATIC",0);g_hBtnClientIcon=child(L"BUTTON",0);g_hBtnClientDrop=child(L"BUTTON",0);g_hBtnGo=child(L"BUTTON",0);g_hBtnPin=child(L"BUTTON",0);g_hBtnRestoreTabs=child(L"BUTTON",0);
    LoadBakedThemesFromHeader();g_themeColors=g_aCustomThemes.front().colors;UpdateThemeBrushes();
    InputRollbackCases();ShortcutIdentityCases();ProfileSwitchCases();
    MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)continue;if((message.message>=WM_KEYFIRST&&message.message<=WM_KEYLAST)||(message.message>=WM_MOUSEFIRST&&message.message<=WM_MOUSELAST))throw std::runtime_error("Unexpected device input in fixture queue");DispatchMessageW(&message);}
    unsigned visible=0;EnumWindows([](HWND window,LPARAM result)->BOOL{DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid==GetCurrentProcessId()&&IsWindowVisible(window))++*reinterpret_cast<unsigned *>(result);return TRUE;},reinterpret_cast<LPARAM>(&visible));
    GUITHREADINFO thread{sizeof(thread)};Check(GetGUIThreadInfo(GetCurrentThreadId(),&thread)&&!thread.hwndActive&&!thread.hwndFocus&&!thread.hwndCapture,"Hidden controls never acquire native activation, focus or input capture");
    Check(!visible&&!nativeShows&&!nativeFocus&&!nativeForeground&&!clipboardRequests&&!launchRequests&&!forbiddenTargets,"No real visible, foreground, focus, clipboard, process, browser or foreign-window action occurs");
    std::cout<<"Launcher background guards: {\"visibleOwnedWindows\":"<<visible<<",\"nativeShowEvents\":"<<nativeShows<<",\"nativeFocusEvents\":"<<nativeFocus<<",\"nativeForegroundEvents\":"<<nativeForeground<<",\"clipboardRequests\":"<<clipboardRequests<<",\"externalProcessRequests\":"<<launchRequests<<",\"foreignWindowRequests\":"<<forbiddenTargets<<",\"nativeEditNotifications\":"<<nativeChanges<<",\"suppressedPopupRequests\":"<<suppressedPopups<<",\"suppressedPresentationRequests\":"<<presentationRequests<<"}\n";
    std::cout<<"Native launcher validation: "<<scenarios<<" scenarios; "<<checks<<" checks; "<<failures<<" failures; native changes="<<nativeChanges<<"; suppressed popups="<<suppressedPopups<<"; suppressed presentation="<<presentationRequests<<"\n";
    DestroyWindow(g_hGui);g_hGui=nullptr;for(const auto hook:hooks)UnhookWinEvent(hook);return failures?1:0;
  }catch(const std::exception &error){std::cerr<<"Fixture setup failure: "<<error.what()<<'\n';for(const auto hook:hooks)UnhookWinEvent(hook);return 2;}
}
