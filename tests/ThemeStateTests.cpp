// Native theme state transitions with private INI files and hidden windows.
// The real application entrypoint and presentation/launch APIs are never used.
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
static unsigned themeSuppressed=0,themeForbidden=0,themeShows=0,themeFocus=0,themeForeground=0;
static unsigned themeNotices=0,themeEnds=0;
static void ThemeOwned(HWND window){DWORD pid=0;if(!window||!IsWindow(window)||!GetWindowThreadProcessId(window,&pid)||pid!=GetCurrentProcessId()){++themeForbidden;throw std::runtime_error("Non-owned theme window refused");}}
static HWND WINAPI ThemeCreate(DWORD ex,LPCWSTR kind,LPCWSTR text,DWORD style,int x,int y,int width,int height,HWND owner,HMENU menu,HINSTANCE instance,LPVOID data){if(style&WS_VISIBLE)++themeSuppressed;return ::CreateWindowExW(ex|WS_EX_NOACTIVATE,kind,text,style&~WS_VISIBLE,x,y,width,height,owner,menu,instance,data);}
static BOOL WINAPI ThemeNoShow(HWND,int){++themeSuppressed;return FALSE;}
static HWND WINAPI ThemeNoFocus(HWND){++themeSuppressed;return nullptr;}
static BOOL WINAPI ThemeNoForeground(HWND){++themeSuppressed;return FALSE;}
static BOOL WINAPI ThemeNoRaise(HWND){++themeSuppressed;return FALSE;}
static BOOL WINAPI ThemePosition(HWND w,HWND after,int x,int y,int width,int height,UINT flags){ThemeOwned(w);if((flags&SWP_SHOWWINDOW)||!(flags&SWP_NOACTIVATE)){++themeSuppressed;return FALSE;}return ::SetWindowPos(w,after,x,y,width,height,flags);}
static BOOL WINAPI ThemeRedraw(HWND w,const RECT *area,HRGN region,UINT flags){ThemeOwned(w);return ::RedrawWindow(w,area,region,flags);}
static BOOL WINAPI ThemeInvalidate(HWND w,const RECT *area,BOOL erase){ThemeOwned(w);return ::InvalidateRect(w,area,erase);}
static BOOL WINAPI ThemePost(HWND w,UINT,WPARAM,LPARAM){ThemeOwned(w);return TRUE;}
static BOOL WINAPI ThemeEnd(HWND w,INT_PTR){ThemeOwned(w);++themeEnds;return ::DestroyWindow(w);}
static BOOL WINAPI ThemeClipboard(HWND){++themeForbidden;return FALSE;}
static HINSTANCE WINAPI ThemeNoShell(HWND,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,INT){++themeForbidden;return reinterpret_cast<HINSTANCE>(ERROR_ACCESS_DENIED);}
static BOOL WINAPI ThemeNoShellEx(SHELLEXECUTEINFOW*){++themeForbidden;return FALSE;}
static BOOL WINAPI ThemeNoProcess(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION){++themeForbidden;return FALSE;}
static LSTATUS WINAPI ThemeNoSystemValue(HKEY,LPCWSTR,LPCWSTR,LPDWORD,LPVOID,LPDWORD){++themeForbidden;return ERROR_ACCESS_DENIED;}
#define CreateWindowExW ThemeCreate
#define ShowWindow ThemeNoShow
#define SetFocus ThemeNoFocus
#define SetForegroundWindow ThemeNoForeground
#define SetActiveWindow ThemeNoFocus
#define BringWindowToTop ThemeNoRaise
#define SetWindowPos ThemePosition
#define RedrawWindow ThemeRedraw
#define InvalidateRect ThemeInvalidate
#define PostMessageW ThemePost
#define EndDialog ThemeEnd
#define OpenClipboard ThemeClipboard
#define ShellExecuteW ThemeNoShell
#define ShellExecuteExW ThemeNoShellEx
#define CreateProcessW ThemeNoProcess
#define SHGetValueW ThemeNoSystemValue
#define CTSPACES_INSTALLER_TEST_HOOKS
#define wWinMain UnusedThemeApplicationEntry
#ifdef CTSPACES_THEME_STATE_BEFORE
#include "../build/app-audit-round8-2026-10-15/theme-state/ctSpaces-before.cpp"
#else
#include "../ctSpaces.cpp"
#endif
#undef wWinMain
#undef EndDialog

int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR text,LPCWSTR,UINT){++themeNotices;std::wcout<<L"Notice: "<<text<<L'\n';return IDOK;}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path&,std::wstring&){return false;}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path&){}
static unsigned themeChecks=0,themeFailures=0,themeCases=0;
static void ThemeCheck(bool value,const char *message){++themeChecks;if(!value)++themeFailures;std::cout<<(value?"PASS ":"FAIL ")<<message<<'\n';}
static std::string ThemeBytes(){std::ifstream file(g_sConfigPath,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
static int ThemeDiskMode(){return GetPrivateProfileIntW(L"user",L"theme",-1,g_sConfigPath.c_str());}
static void CALLBACK ThemeEvent(HWINEVENTHOOK,DWORD event,HWND window,LONG object,LONG,DWORD,DWORD){DWORD pid=0;if(!window||!GetWindowThreadProcessId(window,&pid)||pid!=GetCurrentProcessId())return;if(event==EVENT_OBJECT_SHOW&&object==OBJID_WINDOW)++themeShows;else if(event==EVENT_OBJECT_FOCUS)++themeFocus;else if(event==EVENT_SYSTEM_FOREGROUND)++themeForeground;}
static void ThemeAudit(){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if((msg.message>=WM_KEYFIRST&&msg.message<=WM_KEYLAST)||(msg.message>=WM_MOUSEFIRST&&msg.message<=WM_MOUSELAST))throw std::runtime_error("Device input excluded");if(msg.message!=WM_QUIT)DispatchMessageW(&msg);}unsigned visible=0;EnumWindows([](HWND w,LPARAM value)->BOOL{DWORD pid=0;if(GetWindowThreadProcessId(w,&pid)&&pid==GetCurrentProcessId()&&IsWindowVisible(w))++*reinterpret_cast<unsigned*>(value);return TRUE;},reinterpret_cast<LPARAM>(&visible));GUITHREADINFO state{sizeof(state)};if(!GetGUIThreadInfo(GetCurrentThreadId(),&state)||visible||state.hwndFocus||state.hwndActive||state.hwndCapture||themeShows||themeFocus||themeForeground||themeForbidden)throw std::runtime_error("Background theme safety check failed");}
static INT_PTR CALLBACK ThemeHiddenProc(HWND window,UINT message,WPARAM w,LPARAM l){const auto result=ThemeDlgProc(window,message,w,l);return message==WM_INITDIALOG ? FALSE : result;}
static HWND ThemeOpen(){struct {DLGTEMPLATE dialog;WORD menu=0,kind=0,title=0;} model{};model.dialog.style=WS_POPUP|DS_MODALFRAME;model.dialog.dwExtendedStyle=WS_EX_NOACTIVATE;model.dialog.cx=360;model.dialog.cy=140;const auto window=::CreateDialogIndirectParamW(g_hInst,&model.dialog,g_hGui,ThemeHiddenProc,0);ThemeOwned(window);ThemeAudit();return window;}
static void ThemeSelect(HWND dialog,int mode,bool preview=true){HWND combo=GetDlgItem(dialog,IDC_THEME_COMBO);SendMessageW(combo,CB_SETCURSEL,ThemeComboIndexFromMode(mode),0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_THEME_COMBO,CBN_SELCHANGE),reinterpret_cast<LPARAM>(combo));if(preview)SendMessageW(dialog,WM_TIMER,THEME_PREVIEW_TIMER_ID,0);ThemeAudit();}
static bool ThemeMatches(int mode){const auto &expected=g_aCustomThemes.at(mode-3);return g_themeColors.crWindow==expected.colors.crWindow&&g_themeColors.crControl==expected.colors.crControl&&g_themeColors.crAccent==expected.colors.crAccent&&g_bThemeIsDark==expected.dark;}
static void ThemeSeed(const fs::path &root,int mode){++themeCases;g_sDataDir=root/std::to_wstring(themeCases);fs::create_directory(g_sDataDir);g_sConfigPath=g_sDataDir/L"config.ini";g_bThemeMenuRefreshPending=false;ApplyTheme(mode,false);ThemeCheck(ThemeDiskMode()==mode&&g_iThemeMode==mode&&ThemeMatches(mode),"Private theme baseline commits and renders correctly");}
int wmain(int argc,wchar_t**){std::vector<HWINEVENTHOOK> hooks;try{
  if(argc!=1)throw std::runtime_error("Unknown theme-state argument refused before initialization");
  for(const DWORD event:{EVENT_OBJECT_SHOW,EVENT_OBJECT_FOCUS,EVENT_SYSTEM_FOREGROUND}){auto hook=SetWinEventHook(event,event,nullptr,ThemeEvent,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);if(!hook)throw std::runtime_error("Native theme observer unavailable");hooks.push_back(hook);}
  g_hInst=GetModuleHandleW(nullptr);g_bQaInstance=true;g_sExeDir=GetCurrentExecutablePath().parent_path();INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};if(!InitCommonControlsEx(&controls))throw std::runtime_error("Control initialization failed");
  Gdiplus::GdiplusStartupInput graphics;Gdiplus::GdiplusStartup(&g_gdiplusToken,&graphics,nullptr);LoadBakedThemesFromHeader();if(g_aCustomThemes.size()<3)throw std::runtime_error("Need three custom theme fixtures");
  GUID id{};wchar_t identity[40]{};if(FAILED(CoCreateGuid(&id))||!StringFromGUID2(id,identity,40))throw std::runtime_error("Unique theme fixture identity failed");const auto root=fs::current_path()/L"build"/L"app-audit-round8-2026-10-15"/L"theme-state"/identity;fs::create_directories(root.parent_path());if(!fs::create_directory(root))throw std::runtime_error("Unique private theme fixture could not be created");
  g_hGui=ThemeCreate(WS_EX_NOACTIVATE,L"STATIC",L"Hidden theme state fixture",WS_OVERLAPPED,0,0,600,240,nullptr,nullptr,g_hInst,nullptr);ThemeOwned(g_hGui);
  const int initial=3,selected=4,external=5;
  ThemeSeed(root,initial);HWND dialog=ThemeOpen();const auto original=ThemeBytes();ThemeSelect(dialog,selected);
  ThemeCheck(g_iThemeMode==initial&&ThemeDiskMode()==initial&&ThemeBytes()==original&&ThemeMatches(selected),"Custom preview changes only rendered colors and does not persist a preference");SendMessageW(dialog,WM_COMMAND,IDC_THEME_CANCEL,0);
  ThemeCheck(!IsWindow(dialog)&&g_iThemeMode==initial&&ThemeMatches(initial)&&ThemeBytes()==original,"Ordinary Cancel restores the original theme and exact saved settings");ThemeAudit();
  ThemeSeed(root,initial);dialog=ThemeOpen();ThemeSelect(dialog,selected);const auto lockedBefore=ThemeBytes();HANDLE lock=::CreateFileW(g_sConfigPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("Native private settings lock failed");const auto notices=themeNotices;SendMessageW(dialog,WM_COMMAND,IDC_THEME_APPLY,0);
  std::cout<<"LOCKED APPLY: memory="<<g_iThemeMode<<", disk="<<ThemeDiskMode()<<", open="<<IsWindow(dialog)<<", warnings="<<(themeNotices-notices)<<'\n';
  ThemeCheck(themeNotices==notices+1&&ThemeBytes()==lockedBefore&&ThemeDiskMode()==initial,"A native save failure reports its problem and preserves prior settings bytes");ThemeCheck(g_iThemeMode==initial&&IsWindow(dialog),"Failed Apply keeps the persisted theme identity and leaves the choice available for retry");CloseHandle(lock);if(IsWindow(dialog))SendMessageW(dialog,WM_COMMAND,IDC_THEME_APPLY,0);
  ThemeCheck(!IsWindow(dialog)&&g_iThemeMode==selected&&ThemeDiskMode()==selected&&ThemeMatches(selected)&&themeNotices==notices+1,"Unlocking and retrying the retained Apply choice commits it without another save warning");g_iThemeMode=initial;LoadThemePreference();ThemeCheck(g_iThemeMode==selected,"The successful retry survives a fresh private theme preference reload");ThemeAudit();
  ThemeSeed(root,initial);dialog=ThemeOpen();ThemeSelect(dialog,selected);lock=::CreateFileW(g_sConfigPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("Native cancel settings lock failed");const auto cancelNotices=themeNotices;SendMessageW(dialog,WM_COMMAND,IDC_THEME_CANCEL,0);CloseHandle(lock);
  std::cout<<"LOCKED CANCEL: warnings="<<(themeNotices-cancelNotices)<<'\n';ThemeCheck(themeNotices==cancelNotices&&!IsWindow(dialog)&&g_iThemeMode==initial&&ThemeMatches(initial),"Cancel restores preview without attempting a settings save or raising a save warning");ThemeAudit();
  ThemeSeed(root,initial);dialog=ThemeOpen();ThemeSelect(dialog,selected);std::wstring details;const auto number=std::to_wstring(external),name=GetThemePreferenceName(external);if(!config_persistence::ApplyIniMutationsAtomically(g_sConfigPath,{{L"user",L"theme",number},{L"user",L"theme_name",name}},&details))throw std::runtime_error("Independent private preference write failed");const auto externallySaved=ThemeBytes();SendMessageW(dialog,WM_COMMAND,IDC_THEME_CANCEL,0);
  std::cout<<"CONCURRENT CANCEL: saved newer="<<external<<", after Cancel="<<ThemeDiskMode()<<'\n';ThemeCheck(ThemeBytes()==externallySaved&&ThemeDiskMode()==external,"Cancel cannot overwrite a newer independently saved theme preference");LoadThemePreference();ThemeCheck(g_iThemeMode==external,"Reload observes the independently saved choice after canceled preview");ThemeAudit();
  ThemeSeed(root,initial);dialog=ThemeOpen();ThemeSelect(dialog,selected,false);SendMessageW(dialog,WM_COMMAND,IDC_THEME_APPLY,0);ThemeCheck(!IsWindow(dialog)&&g_iThemeMode==selected&&ThemeDiskMode()==selected&&ThemeMatches(selected),"Apply before the preview timer commits the selected custom theme");g_iThemeMode=initial;LoadThemePreference();ThemeCheck(g_iThemeMode==selected,"A successful Apply survives a private preference reload");ThemeAudit();
  ThemeSeed(root,initial);dialog=ThemeOpen();ThemeSelect(dialog,selected);const auto closeBytes=ThemeBytes();lock=::CreateFileW(g_sConfigPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("Native WM_CLOSE settings lock failed");const auto closeNotices=themeNotices;SendMessageW(dialog,WM_CLOSE,0,0);CloseHandle(lock);
  ThemeCheck(!IsWindow(dialog)&&g_iThemeMode==initial&&ThemeMatches(initial)&&ThemeBytes()==closeBytes&&themeNotices==closeNotices,"WM_CLOSE restores the preview under a settings lock without saving or showing a warning");ThemeAudit();
  ThemeSeed(root,initial);ApplyTheme(selected,true);LoadThemePreference();const auto loadedBytes=ThemeBytes();lock=::CreateFileW(g_sConfigPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("Native loaded-theme settings lock failed");const auto loadedNotices=themeNotices;
#ifdef CTSPACES_THEME_STATE_BEFORE
  ApplyTheme(g_iThemeMode,false);const bool rendered=true;
#else
  const bool rendered=ApplyTheme(g_iThemeMode,false,false);
#endif
  CloseHandle(lock);ThemeCheck(rendered&&g_iThemeMode==initial&&ThemeMatches(initial)&&ThemeBytes()==loadedBytes&&themeNotices==loadedNotices,"A loaded theme fully renders under an INI lock without attempting persistence");ThemeAudit();
#ifndef CTSPACES_THEME_STATE_BEFORE
  ThemeSeed(root,initial);std::wstring malformedDetails;const auto obsolete=std::to_wstring(INT_MAX);
  if(!config_persistence::ApplyIniMutationsAtomically(g_sConfigPath,{{L"user",L"theme",obsolete},{L"user",L"theme_name",L"Removed custom theme"}},&malformedDetails))throw std::runtime_error("Obsolete private theme setting fixture failed");
  LoadThemePreference();ThemeCheck(g_iThemeMode==INT_MAX,"An obsolete numeric theme index reaches startup rendering through the actual preference loader");const auto obsoleteBytes=ThemeBytes();const auto obsoleteNotices=themeNotices;lock=::CreateFileW(g_sConfigPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("Obsolete loaded-theme settings lock failed");
  ThemeColors fallback{};BuildThemeColors(false,fallback);const bool normalized=ApplyTheme(g_iThemeMode,false,false);
  ThemeCheck(normalized&&g_iThemeMode==1&&!g_bThemeIsDark&&g_themeColors.crWindow==fallback.crWindow&&g_themeColors.crAccent==fallback.crAccent&&ThemeBytes()==obsoleteBytes&&themeNotices==obsoleteNotices,"Non-persisting startup rendering normalizes an obsolete index to the rendered fallback without writing locked settings");
  dialog=ThemeOpen();ThemeCheck(SendDlgItemMessageW(dialog,IDC_THEME_COMBO,CB_GETCURSEL,0,0)==ThemeComboIndexFromMode(1),"Reopening Themes selects the actual normalized fallback rather than an unrelated Auto choice");SendMessageW(dialog,WM_CLOSE,0,0);CloseHandle(lock);ThemeAudit();
#endif
  DestroyWindow(g_hGui);g_hGui=nullptr;ThemeAudit();for(const auto hook:hooks)UnhookWinEvent(hook);hooks.clear();Gdiplus::GdiplusShutdown(g_gdiplusToken);
  std::cout<<"Theme state: "<<themeChecks<<" checks, "<<themeCases<<" scenarios, "<<themeFailures<<" failures; suppressed presentation="<<themeSuppressed<<"; native show/focus/foreground="<<themeShows<<'/'<<themeFocus<<'/'<<themeForeground<<"; forbidden="<<themeForbidden<<".\n";std::wcout<<L"FIXTURE="<<root<<L'\n';return themeFailures?1:0;
}catch(const std::exception &error){std::cerr<<"Theme-state safety/setup failure: "<<error.what()<<'\n';for(const auto hook:hooks)UnhookWinEvent(hook);return 2;}}
