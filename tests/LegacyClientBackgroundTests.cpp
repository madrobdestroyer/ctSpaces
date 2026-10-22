// Background variants of older client suites. Test choices and real file
// operations remain unchanged; desktop presentation and external work are inert.
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <vector>
static unsigned backgroundShows=0,backgroundFocus=0,backgroundForeground=0;
static unsigned suppressedPresentation=0,blockedLaunches=0,blockedClipboard=0;
static unsigned suppressedCbtFocus=0,suppressedCbtActivation=0,guardedCbtCreation=0;
static LRESULT CALLBACK BackgroundCbt(int code,WPARAM w,LPARAM l){
 if(code==HCBT_ACTIVATE){++suppressedCbtActivation;return 1;}
 if(code==HCBT_SETFOCUS){++suppressedCbtFocus;return 1;}
 if(code==HCBT_CREATEWND){
  auto *created=reinterpret_cast<CBT_CREATEWNDW*>(l);
  if(created&&created->lpcs){created->lpcs->style&=~WS_VISIBLE;created->lpcs->dwExStyle|=WS_EX_NOACTIVATE;++guardedCbtCreation;}
 }
 return CallNextHookEx(nullptr,code,w,l);
}
static HWND WINAPI BackgroundWindow(DWORD extended,LPCWSTR type,LPCWSTR title,DWORD style,int x,int y,int width,int height,HWND parent,HMENU menu,HINSTANCE instance,LPVOID data){
 return ::CreateWindowExW(extended|WS_EX_NOACTIVATE,type,title,style&~WS_VISIBLE,x,y,width,height,parent,menu,instance,data);
}
struct BackgroundDialogInit{DLGPROC procedure;LPARAM data;};
static constexpr const wchar_t *backgroundDialogProcedure=L"ctSpaces.legacy-background-dialog-procedure";
static INT_PTR CALLBACK BackgroundDialogProc(HWND window,UINT message,WPARAM w,LPARAM l){
 if(message==WM_INITDIALOG){
  const auto &init=*reinterpret_cast<BackgroundDialogInit*>(l);
  if(!SetPropW(window,backgroundDialogProcedure,reinterpret_cast<HANDLE>(init.procedure)))return FALSE;
  init.procedure(window,message,w,init.data);return FALSE;
 }
 const auto procedure=reinterpret_cast<DLGPROC>(GetPropW(window,backgroundDialogProcedure));
 const auto result=procedure?procedure(window,message,w,l):FALSE;
 if(message==WM_NCDESTROY)RemovePropW(window,backgroundDialogProcedure);
 return result;
}
static HWND WINAPI BackgroundDialog(HINSTANCE instance,LPCWSTR name,HWND owner,DLGPROC procedure,LPARAM data){
 const auto resource=FindResourceW(instance,name,RT_DIALOG);const auto loaded=resource?LoadResource(instance,resource):nullptr;
 const DWORD bytes=resource?SizeofResource(instance,resource):0;const auto source=loaded?static_cast<const BYTE*>(LockResource(loaded)):nullptr;
 if(!source||bytes<18)throw std::runtime_error("Hidden legacy dialog resource unavailable");
 std::vector<BYTE> copy(source,source+bytes);WORD signature=0;memcpy(&signature,copy.data()+2,2);
 const size_t styleOffset=signature==0xffff?12:0,extendedOffset=signature==0xffff?8:4;
 DWORD style=0,extended=0;memcpy(&style,copy.data()+styleOffset,4);memcpy(&extended,copy.data()+extendedOffset,4);
 style&=~WS_VISIBLE;extended|=WS_EX_NOACTIVATE;memcpy(copy.data()+styleOffset,&style,4);memcpy(copy.data()+extendedOffset,&extended,4);
 BackgroundDialogInit init{procedure,data};
 return ::CreateDialogIndirectParamW(instance,reinterpret_cast<const DLGTEMPLATE*>(copy.data()),owner,BackgroundDialogProc,reinterpret_cast<LPARAM>(&init));
}
static BOOL WINAPI BackgroundShow(HWND,int){++suppressedPresentation;return FALSE;}
static HWND WINAPI BackgroundFocus(HWND){++suppressedPresentation;return nullptr;}
static BOOL WINAPI BackgroundForeground(HWND){++suppressedPresentation;return FALSE;}
static HWND WINAPI BackgroundActive(HWND){++suppressedPresentation;return nullptr;}
static BOOL WINAPI BackgroundPosition(HWND window,HWND after,int x,int y,int width,int height,UINT flags){
 if(!(flags&SWP_NOACTIVATE)||(flags&SWP_SHOWWINDOW)){++suppressedPresentation;return FALSE;}
 return ::SetWindowPos(window,after,x,y,width,height,flags);
}
static HDWP WINAPI BackgroundDefer(HDWP deferred,HWND window,HWND after,int x,int y,int width,int height,UINT flags){
 if(!(flags&SWP_NOACTIVATE)||(flags&SWP_SHOWWINDOW)){++suppressedPresentation;return nullptr;}
 return ::DeferWindowPos(deferred,window,after,x,y,width,height,flags);
}
static BOOL WINAPI BackgroundInvalidate(HWND,const RECT*,BOOL){return FALSE;}
static BOOL WINAPI BackgroundRedraw(HWND,const RECT*,HRGN,UINT){return FALSE;}
static BOOL WINAPI BackgroundPost(HWND,UINT,WPARAM,LPARAM){return FALSE;}
static BOOL WINAPI BackgroundClipboard(HWND){++blockedClipboard;return FALSE;}
static HINSTANCE WINAPI BackgroundShell(HWND,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,INT){++blockedLaunches;return reinterpret_cast<HINSTANCE>(ERROR_ACCESS_DENIED);}
static BOOL WINAPI BackgroundShellEx(SHELLEXECUTEINFOW*){++blockedLaunches;return FALSE;}
static BOOL WINAPI BackgroundProcess(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION){++blockedLaunches;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static LSTATUS WINAPI BackgroundRegistry(HKEY,LPCWSTR,LPCWSTR,LPDWORD,LPVOID,LPDWORD){return ERROR_ACCESS_DENIED;}
static HANDLE WINAPI BackgroundSnapshot(DWORD,DWORD){return CreateEventW(nullptr,FALSE,FALSE,nullptr);}
static BOOL WINAPI BackgroundFirst(HANDLE,LPPROCESSENTRY32W item){wcscpy_s(item->szExeFile,L"ctSpaces-owned-background-fixture.exe");item->th32ProcessID=GetCurrentProcessId();return TRUE;}
static BOOL WINAPI BackgroundNext(HANDLE,LPPROCESSENTRY32W){SetLastError(ERROR_NO_MORE_FILES);return FALSE;}
#define ShowWindow BackgroundShow
#define SetFocus BackgroundFocus
#define SetForegroundWindow BackgroundForeground
#define SetActiveWindow BackgroundActive
#define BringWindowToTop BackgroundForeground
#define SetWindowPos BackgroundPosition
#define DeferWindowPos BackgroundDefer
#define InvalidateRect BackgroundInvalidate
#define RedrawWindow BackgroundRedraw
#define PostMessageW BackgroundPost
#define OpenClipboard BackgroundClipboard
#define ShellExecuteW BackgroundShell
#define ShellExecuteExW BackgroundShellEx
#define CreateProcessW BackgroundProcess
#define SHGetValueW BackgroundRegistry
#define CreateToolhelp32Snapshot BackgroundSnapshot
#define Process32FirstW BackgroundFirst
#define Process32NextW BackgroundNext
#define CreateWindowExW BackgroundWindow
#define CreateDialogParamW BackgroundDialog
#define wmain LegacyClientMain
#if defined(CTSPACES_BACKGROUND_CLIENT_ISOLATION)
#include "ClientIsolationTests.cpp"
#elif defined(CTSPACES_BACKGROUND_CLIENT_RENAME_UNICODE)
#include "ClientRenameUnicodeTests.cpp"
#elif defined(CTSPACES_BACKGROUND_CLIENT_IDENTITY_UNICODE)
// Its nested helper import redefines wmain; only the outer entry declaration
// is renamed in the generated copy, retaining its complete original body.
#include "IdentityBackgroundEntry.inl"
#else
#error Unsupported legacy client background suite
#endif
#undef wmain
static void CALLBACK BackgroundEvent(HWINEVENTHOOK,DWORD event,HWND,LONG object,LONG,DWORD,DWORD){
 if(event==EVENT_SYSTEM_FOREGROUND)++backgroundForeground;
 else if(event==EVENT_OBJECT_FOCUS)++backgroundFocus;
 else if(event==EVENT_OBJECT_SHOW&&object==OBJID_WINDOW)++backgroundShows;
}
int wmain(int argc,wchar_t**argv){
 std::vector<HWINEVENTHOOK> hooks;
 for(DWORD event:{EVENT_OBJECT_SHOW,EVENT_OBJECT_FOCUS,EVENT_SYSTEM_FOREGROUND}){
  auto hook=SetWinEventHook(event,event,nullptr,BackgroundEvent,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);
  if(!hook){for(auto prior:hooks)UnhookWinEvent(prior);return 90;}hooks.push_back(hook);
 }
 // This hook is limited to this test GUI thread. It vetoes native dialog
 // manager activation/focus before it happens and never hooks user threads.
 const HHOOK cbt=SetWindowsHookExW(WH_CBT,BackgroundCbt,nullptr,GetCurrentThreadId());
 if(!cbt){for(auto hook:hooks)UnhookWinEvent(hook);return 93;}
#if defined(CTSPACES_BACKGROUND_CLIENT_ISOLATION)
 const int result=LegacyClientMain(argc,argv);
#else
 (void)argc;(void)argv;const int result=LegacyClientMain();
#endif
 MSG message{};bool deviceMessage=false;
 while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
  if((message.message>=WM_KEYFIRST&&message.message<=WM_KEYLAST)||(message.message>=WM_MOUSEFIRST&&message.message<=WM_MOUSELAST)){deviceMessage=true;continue;}
  DispatchMessageW(&message);
 }
 unsigned visible=0;EnumWindows([](HWND window,LPARAM count)->BOOL{DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid==GetCurrentProcessId()&&IsWindowVisible(window))++*reinterpret_cast<unsigned*>(count);return TRUE;},reinterpret_cast<LPARAM>(&visible));
 GUITHREADINFO ui{sizeof(ui)};
 const bool safe=GetGUIThreadInfo(GetCurrentThreadId(),&ui)&&!ui.hwndActive&&!ui.hwndFocus&&!ui.hwndCapture&&!backgroundShows&&!backgroundFocus&&!backgroundForeground&&!visible&&!deviceMessage&&!blockedLaunches&&!blockedClipboard;
 for(auto hook:hooks)UnhookWinEvent(hook);
 UnhookWindowsHookEx(cbt);
 std::cout<<"Legacy client background audit: shows="<<backgroundShows<<" focus="<<backgroundFocus<<" foreground="<<backgroundForeground<<" visible="<<visible<<" launch="<<blockedLaunches<<" clipboard="<<blockedClipboard<<"; presentation requests suppressed="<<suppressedPresentation<<'\n';
 std::cout<<"Native activation/focus attempts vetoed by test-thread CBT="<<suppressedCbtActivation<<"/"<<suppressedCbtFocus<<"; guarded window creations="<<guardedCbtCreation<<'\n';
 return safe?result:92;
}
