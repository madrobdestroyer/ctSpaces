#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shldisp.h>
#include <shobjidl.h>
#include <deque>
#include "../ProgressUI.h"
static bool backgroundNotesTests=false;
static unsigned suppressedVisibility=0,suppressedFocus=0,suppressedProgress=0;
namespace notes_test {
BOOL WINAPI Show(HWND,int);
BOOL WINAPI Position(HWND,HWND,int,int,int,int,UINT);
HWND WINAPI Focus(HWND);
BOOL WINAPI Foreground(HWND);
void ProgressShow(HWND,const std::wstring &,int);
INT_PTR WINAPI Dialog(HINSTANCE,LPCWSTR,HWND,DLGPROC,LPARAM);
HWND WINAPI Modeless(HINSTANCE,LPCWSTR,HWND,DLGPROC,LPARAM);
BOOL WINAPI PickFile(OPENFILENAMEW *);
BOOL WINAPI Print(PRINTDLGW *);
HINSTANCE WINAPI ShellOpen(HWND,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,INT);
BOOL WINAPI Tray(DWORD,PNOTIFYICONDATAW);
BOOL WINAPI Popup(HMENU,UINT,int,int,HWND,LPTPMPARAMS);
HWND WINAPI CreateNoteWindow(DWORD,LPCWSTR,LPCWSTR,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,LPVOID);
HDWP WINAPI BeginPosition(int);
HDWP WINAPI DeferPosition(HDWP,HWND,HWND,int,int,int,int,UINT);
BOOL WINAPI EndPosition(HDWP);
BOOL WINAPI MonitorInfo(HMONITOR,LPMONITORINFO);
HWND WINAPI Active(HWND);
BOOL WINAPI Raise(HWND);
HWND WINAPI Capture(HWND);
BOOL WINAPI Release();
BOOL WINAPI ClipboardOpen(HWND);
BOOL WINAPI ClipboardEmpty();
BOOL WINAPI ClipboardClose();
HANDLE WINAPI ClipboardSet(UINT,HANDLE);
HANDLE WINAPI ClipboardGet(UINT);
BOOL WINAPI ClipboardAvailable(UINT);
BOOL WINAPI KeyboardState(const BYTE *);
UINT WINAPI Input(UINT,LPINPUT,int);
BOOL WINAPI PaintPublish(HDC,int,int,int,int,HDC,int,int,DWORD);
HBITMAP WINAPI PaintBitmap(HDC,int,int);
HGDIOBJ WINAPI PaintSelect(HDC,HGDIOBJ);
BOOL WINAPI PaintViewport(HDC,int,int,LPPOINT);
HDC WINAPI PaintBegin(HWND,LPPAINTSTRUCT);
BOOL WINAPI PaintEnd(HWND,const PAINTSTRUCT *);
int WINAPI PaintFrame(HDC,const RECT *,HBRUSH);
BOOL WINAPI InvalidateOwned(HWND,const RECT *,BOOL);
BOOL WINAPI RedrawOwned(HWND,const RECT *,HRGN,UINT);
}
#define CTSPACES_INSTALLER_TEST_HOOKS
#define CTSPACES_NOTES_TEST_HOOKS
#define wWinMain CtSpacesProductionWinMain
#define DialogBoxParamW notes_test::Dialog
#define CreateDialogParamW notes_test::Modeless
#define GetOpenFileNameW notes_test::PickFile
#define GetSaveFileNameW notes_test::PickFile
#define PrintDlgW notes_test::Print
#define ShellExecuteW notes_test::ShellOpen
#define Shell_NotifyIconW notes_test::Tray
#define TrackPopupMenuEx notes_test::Popup
#define CreateWindowExW notes_test::CreateNoteWindow
#define ShowWindow notes_test::Show
#define SetWindowPos notes_test::Position
#define SetFocus notes_test::Focus
#define SetForegroundWindow notes_test::Foreground
#define SetActiveWindow notes_test::Active
#define BringWindowToTop notes_test::Raise
#define SetCapture notes_test::Capture
#define ReleaseCapture notes_test::Release
#define OpenClipboard notes_test::ClipboardOpen
#define EmptyClipboard notes_test::ClipboardEmpty
#define CloseClipboard notes_test::ClipboardClose
#define SetClipboardData notes_test::ClipboardSet
// Native clipboard reads in production are behind the guarded OpenClipboard;
// GetClipboardData is also an OLE method name and cannot be macro-replaced here.
#define IsClipboardFormatAvailable notes_test::ClipboardAvailable
#define SetKeyboardState notes_test::KeyboardState
#define SendInput notes_test::Input
#define ProgressUI_Show notes_test::ProgressShow
#define BeginDeferWindowPos notes_test::BeginPosition
#define DeferWindowPos notes_test::DeferPosition
#define EndDeferWindowPos notes_test::EndPosition
#define GetMonitorInfoW notes_test::MonitorInfo
#define BitBlt notes_test::PaintPublish
#define CreateCompatibleBitmap notes_test::PaintBitmap
#define SelectObject notes_test::PaintSelect
#define SetViewportOrgEx notes_test::PaintViewport
#define BeginPaint notes_test::PaintBegin
#define EndPaint notes_test::PaintEnd
#define FrameRect notes_test::PaintFrame
#define InvalidateRect notes_test::InvalidateOwned
#define RedrawWindow notes_test::RedrawOwned
#if defined(CTSPACES_NOTES_PRODUCTION_SOURCE)
#include CTSPACES_NOTES_PRODUCTION_SOURCE
#elif defined(CTSPACES_REDRAW_BASELINE)
#include "../build/redraw-regression-2026-10-15/pinned-repaint/ctSpaces-before.cpp"
#else
#include "../ctSpaces.cpp"
#endif
#undef DialogBoxParamW
#undef CreateDialogParamW
#undef GetOpenFileNameW
#undef GetSaveFileNameW
#undef PrintDlgW
#undef ShellExecuteW
#undef Shell_NotifyIconW
#undef TrackPopupMenuEx
#undef CreateWindowExW
#undef ShowWindow
#undef SetWindowPos
#undef SetFocus
#undef SetForegroundWindow
#undef SetActiveWindow
#undef BringWindowToTop
#undef SetCapture
#undef ReleaseCapture
#undef OpenClipboard
#undef EmptyClipboard
#undef CloseClipboard
#undef SetClipboardData
#undef GetClipboardData
#undef IsClipboardFormatAvailable
#undef SetKeyboardState
#undef SendInput
#undef ProgressUI_Show
#undef BeginDeferWindowPos
#undef DeferWindowPos
#undef EndDeferWindowPos
#undef GetMonitorInfoW
#undef BitBlt
#undef CreateCompatibleBitmap
#undef SelectObject
#undef SetViewportOrgEx
#undef BeginPaint
#undef EndPaint
#undef FrameRect
#undef InvalidateRect
#undef RedrawWindow
#undef wWinMain

#include "NotesBackgroundGuards.inl"
// Printer UI is opt-in only for the private memory-DC print fixture. Ordinary
// background suites can never open a print dialog or submit a real print job.
static std::function<BOOL(PRINTDLGW *)> notesPrintDialogObserver;
static unsigned notesSuppressedPrintDialogs=0;
BOOL WINAPI notes_test::Print(PRINTDLGW *choices) {
  if(notesPrintDialogObserver)return notesPrintDialogObserver(choices);
  if(backgroundNotesTests){++notesSuppressedPrintDialogs;return FALSE;}
  return ::PrintDlgW(choices);
}

static bool notesObserveRepaintRequests=false;
static unsigned notesObservedInvalidates=0,notesObservedRedraws=0;
static HWND notesInvalidationCaptureWindow=nullptr;
static HRGN notesInvalidationCaptureRegion=nullptr;
static bool notesInvalidationCaptureErase=false;
static void NotesRequireOwnedRepaint(HWND window) {
  if(!backgroundNotesTests) return;
  DWORD pid=0;
  if(!window || !IsWindow(window) || !GetWindowThreadProcessId(window,&pid) || pid!=GetCurrentProcessId()) {
    ++backgroundGuard.forbiddenRedraw;
    throw std::runtime_error("Background redraw refused before native API: window is null, destroyed or not owned by the test process");
  }
}
BOOL WINAPI notes_test::InvalidateOwned(HWND window,const RECT *rect,BOOL erase) {
  NotesRequireOwnedRepaint(window);if(notesObserveRepaintRequests) ++notesObservedInvalidates;
  if(window==notesInvalidationCaptureWindow && notesInvalidationCaptureRegion) {
    RECT area{};if(rect) area=*rect;else GetClientRect(window,&area);
    HRGN requested=CreateRectRgnIndirect(&area);
    if(!requested || CombineRgn(notesInvalidationCaptureRegion,notesInvalidationCaptureRegion,requested,RGN_OR)==ERROR)
      throw std::runtime_error("Library invalidation observation failed");
    DeleteObject(requested);notesInvalidationCaptureErase|=erase!=FALSE;
  }
  return ::InvalidateRect(window,rect,erase);
}
BOOL WINAPI notes_test::RedrawOwned(HWND window,const RECT *rect,HRGN region,UINT flags) {
  NotesRequireOwnedRepaint(window);if(notesObserveRepaintRequests) ++notesObservedRedraws;
  return ::RedrawWindow(window,rect,region,flags);
}

struct NotesShowRequest { HWND window;int command;bool registered; };
static std::vector<NotesShowRequest> notesShowRequests;
static HDC observedPaintTarget=nullptr;
static unsigned observedPaintPublishes=0;
static bool failNextPaintBitmap=false;
static bool failNextPaintSelection=false,failNextPaintViewport=false;
// This target is set only by a hidden, process-owned paint fixture. The real
// BeginPaint/EndPaint pair still validates the native update region; the
// production paint procedure renders into the explicitly clipped test DIB.
static HWND notesVirtualPaintWindow=nullptr;
static HDC notesVirtualPaintDc=nullptr,notesVirtualNativeDc=nullptr;
static int notesVirtualSavedDc=0;
static unsigned notesVirtualPaintBegins=0,notesVirtualPaintEnds=0;
static unsigned notesVirtualPaintFrames=0;
static std::vector<std::pair<RECT,HBRUSH>> notesVirtualPaintFrameRects;
static int notesVirtualUpdateKind=ERROR;
static RECT notesVirtualUpdateBounds{},notesVirtualNativeClip{},notesVirtualPaintRect{};
static bool notesVirtualClipRequested=false,notesVirtualPaintActive=false;
static RECT notesVirtualRequestedDirty{};
static PAINTSTRUCT notesVirtualNativePaint{};
int WINAPI notes_test::PaintFrame(HDC dc,const RECT *area,HBRUSH brush) {
  if(notesVirtualPaintActive) {
    ++notesVirtualPaintFrames;
    notesVirtualPaintFrameRects.emplace_back(*area,brush);
  }
  return ::FrameRect(dc,area,brush);
}
HDC WINAPI notes_test::PaintBegin(HWND window,LPPAINTSTRUCT paint) {
  HRGN dirty=nullptr;
  if(window==notesVirtualPaintWindow && notesVirtualPaintDc) {
    dirty=CreateRectRgn(0,0,0,0);notesVirtualUpdateKind=GetUpdateRgn(window,dirty,FALSE);
    GetRgnBox(dirty,&notesVirtualUpdateBounds);
  }
  HDC native=::BeginPaint(window,paint);
  if(dirty) {
    GetClipBox(native,&notesVirtualNativeClip);notesVirtualPaintRect=paint->rcPaint;
    notesVirtualNativePaint=*paint;notesVirtualPaintActive=true;
    // USER can discard update regions for a window that has never been shown.
    // In that case a native BeginPaint has no drawable pixels. An explicitly
    // requested in-memory paint region exercises the unchanged production
    // painter while retaining the native BeginPaint/EndPaint validation pair.
    // It is an instrumented paint target, not a visible-window update test.
    if(notesVirtualClipRequested) {
      SetRectRgn(dirty,notesVirtualRequestedDirty.left,notesVirtualRequestedDirty.top,
          notesVirtualRequestedDirty.right,notesVirtualRequestedDirty.bottom);
      paint->rcPaint=notesVirtualRequestedDirty;
    }
    notesVirtualNativeDc=native;notesVirtualSavedDc=SaveDC(notesVirtualPaintDc);
    SelectClipRgn(notesVirtualPaintDc,dirty);DeleteObject(dirty);
    paint->hdc=notesVirtualPaintDc;++notesVirtualPaintBegins;return notesVirtualPaintDc;
  }
  return native;
}
BOOL WINAPI notes_test::PaintEnd(HWND window,const PAINTSTRUCT *paint) {
  if(window==notesVirtualPaintWindow && notesVirtualPaintDc && notesVirtualPaintActive) {
    if(notesVirtualSavedDc) RestoreDC(notesVirtualPaintDc,notesVirtualSavedDc);
    notesVirtualSavedDc=0;notesVirtualNativeDc=nullptr;notesVirtualPaintActive=false;++notesVirtualPaintEnds;
    return ::EndPaint(window,&notesVirtualNativePaint);
  }
  return ::EndPaint(window,paint);
}
BOOL WINAPI notes_test::PaintPublish(HDC target,int x,int y,int width,int height,HDC source,int sourceX,int sourceY,DWORD operation) {
  if(target==observedPaintTarget) ++observedPaintPublishes;
  return ::BitBlt(target,x,y,width,height,source,sourceX,sourceY,operation);
}
HBITMAP WINAPI notes_test::PaintBitmap(HDC target,int width,int height) {
  if(failNextPaintBitmap){failNextPaintBitmap=false;SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;}
  return ::CreateCompatibleBitmap(target,width,height);
}
HGDIOBJ WINAPI notes_test::PaintSelect(HDC target,HGDIOBJ object) {
  if(failNextPaintSelection && GetObjectType(object)==OBJ_BITMAP) {failNextPaintSelection=false;SetLastError(ERROR_NOT_ENOUGH_MEMORY);return HGDI_ERROR;}
  return ::SelectObject(target,object);
}
BOOL WINAPI notes_test::PaintViewport(HDC target,int x,int y,LPPOINT previous) {
  if(failNextPaintViewport){failNextPaintViewport=false;SetLastError(ERROR_NOT_ENOUGH_MEMORY);return FALSE;}
  return ::SetViewportOrgEx(target,x,y,previous);
}
BOOL WINAPI notes_test::Show(HWND window,int command) {
  if(backgroundNotesTests) notesShowRequests.push_back({window,command,window==g_hNotesWindow});
  if(backgroundNotesTests) {++backgroundGuard.showRequested;++backgroundGuard.showSuppressed;}
  if(backgroundNotesTests) {if(command!=SW_HIDE) ++suppressedVisibility;return FALSE;}
  return ::ShowWindow(window,command);
}
BOOL WINAPI notes_test::Position(HWND window,HWND order,int x,int y,int width,int height,UINT flags) {
  if(backgroundNotesTests && (flags&SWP_SHOWWINDOW)) {++backgroundGuard.positionShowRequested;++backgroundGuard.positionShowSuppressed;}
  if(backgroundNotesTests) {if(flags&SWP_SHOWWINDOW) ++suppressedVisibility;flags=(flags&~SWP_SHOWWINDOW)|SWP_NOACTIVATE;order=nullptr;flags|=SWP_NOZORDER;}
  return ::SetWindowPos(window,order,x,y,width,height,flags);
}
HWND WINAPI notes_test::Focus(HWND window) {
  if(backgroundNotesTests) {++backgroundGuard.focusRequested;++backgroundGuard.focusSuppressed;}
  if(backgroundNotesTests) {++suppressedFocus;return nullptr;}return ::SetFocus(window);
}
BOOL WINAPI notes_test::Foreground(HWND window) {
  if(backgroundNotesTests) {++backgroundGuard.foregroundRequested;++backgroundGuard.foregroundSuppressed;}
  if(backgroundNotesTests) {++suppressedFocus;return FALSE;}return ::SetForegroundWindow(window);
}
void notes_test::ProgressShow(HWND owner,const std::wstring &text,int percent) {
  if(backgroundNotesTests) {++backgroundGuard.progressRequested;++backgroundGuard.progressSuppressed;}
  if(backgroundNotesTests) {++suppressedProgress;return;}::ProgressUI_Show(owner,text,percent);
}
static int geometryFault=0,geometryDeferCalls=0;
static bool geometryFaultHit=false;
static RECT geometryWork{};
HDWP WINAPI notes_test::BeginPosition(int count) {
  geometryDeferCalls=0;
  if(geometryFault==1) {geometryFault=0;geometryFaultHit=true;SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;}
  return ::BeginDeferWindowPos(count);
}
HDWP WINAPI notes_test::DeferPosition(HDWP batch,HWND child,HWND order,int x,int y,int width,int height,UINT flags) {
  if(geometryFault==2 && ++geometryDeferCalls==3) {geometryFault=0;geometryFaultHit=true;return ::DeferWindowPos(batch,reinterpret_cast<HWND>(static_cast<INT_PTR>(-5)),nullptr,0,0,0,0,SWP_NOZORDER);}
  if(backgroundNotesTests){if(flags&SWP_SHOWWINDOW){++backgroundGuard.positionShowRequested;++backgroundGuard.positionShowSuppressed;}flags=(flags&~SWP_SHOWWINDOW)|SWP_NOACTIVATE|SWP_NOZORDER;order=nullptr;}
  return ::DeferWindowPos(batch,child,order,x,y,width,height,flags);
}
BOOL WINAPI notes_test::EndPosition(HDWP batch) {
  if(geometryFault==3) {geometryFault=0;geometryFaultHit=true;const auto invalid=::DeferWindowPos(batch,reinterpret_cast<HWND>(static_cast<INT_PTR>(-5)),nullptr,0,0,0,0,SWP_NOZORDER);if(invalid) ::EndDeferWindowPos(invalid);return FALSE;}
  return ::EndDeferWindowPos(batch);
}
BOOL WINAPI notes_test::MonitorInfo(HMONITOR monitor,LPMONITORINFO info) {
  const auto result=::GetMonitorInfoW(monitor,info);
  if(result && geometryWork.right>geometryWork.left && geometryWork.bottom>geometryWork.top) info->rcWork=geometryWork;
  return result;
}

static std::deque<int> answers;
static std::function<INT_PTR(LPCWSTR,DLGPROC,LPARAM)> notesDialogObserver;
static std::function<BOOL(OPENFILENAMEW *)> notesFilePickerObserver;
static std::deque<std::pair<std::wstring,std::wstring>> inputs;
static std::deque<std::wstring> files;
static std::wstring lastNotice;
static unsigned walkthroughOpens=0;
static unsigned shellOpens=0,metadataOpens=0;
static int openedLibraryFilter=-1;
static uint64_t openedLibraryNote=0;
static std::wstring shellTarget;
static std::wstring shellParameters;
static bool shellHasParameters=false;
static INT_PTR shellResult=33;
static bool trayAddWorks=true,trayVersionWorks=true;
static bool realTrayApi=false;
static std::vector<DWORD> trayCommands;
static UINT popupAction=0,popupCalls=0;
static std::function<void(HMENU)> notesPopupObserver;
static bool popupPasteEnabled=false,popupCutEnabled=false;
static bool popupLeftEnabled=false,popupRightEnabled=false;
static bool failNextNotesEditor=false;
static unsigned explicitRichEditCreates=0;
static std::function<void(HWND,HWND,int,int)> notesRichEditCreatedObserver;
HWND WINAPI notes_test::CreateNoteWindow(DWORD ex,LPCWSTR type,LPCWSTR title,DWORD style,int x,int y,int width,int height,HWND parent,HMENU menu,HINSTANCE instance,LPVOID data) {
  if(backgroundNotesTests) {if(style&WS_VISIBLE){++backgroundGuard.creationVisibleRequested;++backgroundGuard.creationVisibleSuppressed;}style&=~WS_VISIBLE;ex|=WS_EX_NOACTIVATE;}
  if (HIWORD(type) && !wcscmp(type,L"RICHEDIT50W")) ++explicitRichEditCreates;
  if (failNextNotesEditor && parent && menu==reinterpret_cast<HMENU>(IDC_CLIENT_NOTES_TEXT) &&
      HIWORD(type) && !wcscmp(type,L"RICHEDIT50W")) {
    failNextNotesEditor=false;SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;
  }
  const auto window=::CreateWindowExW(ex,type,title,style,x,y,width,height,parent,menu,instance,data);
  if(backgroundNotesTests && window) ++backgroundGuard.windowsCreated;
  if(backgroundNotesTests && window && !(style&WS_CHILD)) backgroundTopWindows.insert(window);
  if(backgroundNotesTests && window && HIWORD(type) && !wcscmp(type,L"RICHEDIT50W")) SetWindowSubclass(window,NotesBackgroundRichGuard,0x4E424747,0);
  if(backgroundNotesTests && window && HIWORD(type) && !wcscmp(type,L"RICHEDIT50W") && notesRichEditCreatedObserver)notesRichEditCreatedObserver(window,parent,width,height);
  return window;
}
BOOL WINAPI notes_test::Tray(DWORD command,PNOTIFYICONDATAW icon) {
  if(backgroundNotesTests && realTrayApi) throw std::runtime_error("Background scope cannot use the actual notification area");
  if (!icon || icon->cbSize!=sizeof(*icon) || icon->uID!=1) throw std::runtime_error("Unexpected tray identity");
  trayCommands.push_back(command);
  if (realTrayApi) return ::Shell_NotifyIconW(command,icon);
  if (command==NIM_ADD) return trayAddWorks;
  if (command==NIM_SETVERSION) return trayVersionWorks && icon->uVersion==NOTIFYICON_VERSION_4;
  return TRUE;
}
BOOL WINAPI notes_test::Popup(HMENU menu,UINT,int,int,HWND,LPTPMPARAMS) {
  if(notesPopupObserver) notesPopupObserver(menu);
  ++popupCalls;popupPasteEnabled=(GetMenuState(menu,5,MF_BYCOMMAND)&MF_GRAYED)==0;
  popupCutEnabled=(GetMenuState(menu,3,MF_BYCOMMAND)&MF_GRAYED)==0;
  popupLeftEnabled=(GetMenuState(menu,IDC_NOTES_TAB_LEFT,MF_BYCOMMAND)&MF_GRAYED)==0;
  popupRightEnabled=(GetMenuState(menu,IDC_NOTES_TAB_RIGHT,MF_BYCOMMAND)&MF_GRAYED)==0;
  const UINT action=popupAction;popupAction=0;return action;
}
HINSTANCE WINAPI notes_test::ShellOpen(HWND,LPCWSTR verb,LPCWSTR target,LPCWSTR parameters,LPCWSTR directory,INT) {
  if (wcscmp(verb,L"open") || directory) throw std::runtime_error("Unexpected shell URL arguments");
  ++shellOpens;shellTarget=target;shellHasParameters=parameters!=nullptr;shellParameters=parameters ? parameters : L"";
  return reinterpret_cast<HINSTANCE>(shellResult);
}
static std::function<void()> beforeFileSelection;
static std::function<void(const fs::path &)> afterNoteCommit;
void CtSpacesNotesTestAfterCommit(const wchar_t *root) {
  if(afterNoteCommit) {auto hook=std::move(afterNoteCommit);afterNoteCommit={};hook(fs::path(root));}
}
BOOL WINAPI notes_test::PickFile(OPENFILENAMEW *file) {
  if(notesFilePickerObserver) return notesFilePickerObserver(file);
  if (files.empty()) throw std::runtime_error("Unexpected file picker");
  if (beforeFileSelection) beforeFileSelection();
  wcscpy_s(file->lpstrFile,file->nMaxFile,files.front().c_str()); files.pop_front(); return TRUE;
}
static unsigned checks=0;
static void Check(bool condition,const char *name) {
  if (!condition) throw std::runtime_error(name);
  ++checks; std::cout << "PASS " << name << std::endl;
}
int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR text,LPCWSTR,UINT flags) {
  lastNotice=text;
  std::wcout << L"Notice: " << text << std::endl;
  if (!answers.empty()) { int result=answers.front(); answers.pop_front(); return result; }
  return (flags & MB_YESNO) ? IDNO : IDOK;
}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path &,std::wstring &) { return false; }
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path &) {}
INT_PTR WINAPI notes_test::Dialog(HINSTANCE,LPCWSTR resource,HWND,DLGPROC procedure,LPARAM data) {
  if(backgroundNotesTests) ++backgroundGuard.modalSuppressed;
  if(notesDialogObserver) return notesDialogObserver(resource,procedure,data);
  if (resource==MAKEINTRESOURCEW(IDD_NOTES_HELP)) return IDOK;
  if (resource==MAKEINTRESOURCEW(IDD_NOTES_WALKTHROUGH)) { ++walkthroughOpens; return IDOK; }
  if (resource==MAKEINTRESOURCEW(IDD_NOTES_METADATA)) {++metadataOpens;return IDCANCEL;}
  if (resource==MAKEINTRESOURCEW(IDD_NOTES_LIBRARY) && procedure==NotesLibraryProc) {
    const auto &library=*reinterpret_cast<NotesLibrary *>(data);openedLibraryFilter=library.initialFilter;openedLibraryNote=library.initialNote;return IDCANCEL;
  }
  if (resource==MAKEINTRESOURCEW(IDD_NOTES_CODE_BLOCK) && !inputs.empty()) {
    auto *input=reinterpret_cast<NotesCodeInput *>(data);input->language=4;input->source=inputs.front().second;inputs.pop_front();return IDOK;
  }
  if (resource==MAKEINTRESOURCEW(IDD_NOTES_TEMPLATE_EDITOR) && !inputs.empty()) {
    HWND manager=notes_test::Modeless(g_hInst,resource,nullptr,NotesTemplateEditorProc,data);
    const auto values=inputs.front();inputs.pop_front();
    SetDlgItemTextW(manager,IDC_NOTES_TEMPLATE_NAME,values.first.c_str());
    SetDlgItemTextW(manager,IDC_NOTES_TEMPLATE_HINT,values.second.c_str());
    const bool saved=NotesTemplateEditorSave(manager,*reinterpret_cast<NotesTemplates *>(data));
    DestroyWindow(manager);return saved ? IDOK : IDCANCEL;
  }
  if (resource==MAKEINTRESOURCEW(IDD_NOTES_CLIENT_PICKER) && !inputs.empty()) {
    auto *picker=reinterpret_cast<NotesClientPicker *>(data);
    picker->selected=inputs.front().first; inputs.pop_front(); return IDOK;
  }
  if (resource==MAKEINTRESOURCEW(IDD_NOTES_INPUT) && !inputs.empty()) {
    auto *input=reinterpret_cast<NotesInput *>(data);
    input->first=inputs.front().first; input->second=inputs.front().second; inputs.pop_front(); return IDOK;
  }
  throw std::runtime_error("Unexpected modal dialog in test");
}
// Apply the same guards to test-owned native calls after the production include.
// The default foreground suite still takes the original Win32 paths.
#define CreateDialogParamW notes_test::Modeless
#define CreateWindowExW notes_test::CreateNoteWindow
#define ShowWindow notes_test::Show
#define SetWindowPos notes_test::Position
#define SetFocus notes_test::Focus
#define SetForegroundWindow notes_test::Foreground
#define SetActiveWindow notes_test::Active
#define BringWindowToTop notes_test::Raise
#define SetCapture notes_test::Capture
#define ReleaseCapture notes_test::Release
#define OpenClipboard notes_test::ClipboardOpen
#define EmptyClipboard notes_test::ClipboardEmpty
#define SetClipboardData notes_test::ClipboardSet
#define GetClipboardData notes_test::ClipboardGet
#define IsClipboardFormatAvailable notes_test::ClipboardAvailable
#define SetKeyboardState notes_test::KeyboardState
#define SendInput notes_test::Input
#define InvalidateRect notes_test::InvalidateOwned
#define RedrawWindow notes_test::RedrawOwned
static void Put(const fs::path &path,const std::string &bytes) {
  fs::create_directories(path.parent_path()); std::ofstream(path,std::ios::binary)<<bytes;
}
static void Fixture(const std::wstring &name) {
  const auto root=g_sDataDir/L"Sites"/name;
  Put(root/L"ctSpaces-client-v2","ctSpaces-client-schema=2\r\n");
  Put(root/L"Browsers/edge/ctSpaces-browser-v2","ctSpaces-browser-schema=2\r\nbrowser=edge\r\n");
  Put(root/L"Browsers/edge/Profile/ctSpaces","ctSpaces-profile=2\r\n");
  Put(root/L"Browsers/edge/Profile/Default/Preferences","{}");
}
static HWND Open(const std::wstring &name) {
  const auto root=g_sDataDir/L"Sites"/name;
  Check(IsExistingClientProfile(name),"Disposable client recognized");
  auto *state=new ClientNotesDialogState{name,root,client_notes::ReadNotebook(root)};
  state->draftName=NotesNewDraftName();
  Check(!state->draftName.empty(),"A unique production recovery session name is prepared");
  state->richEditModule=LoadLibraryW(L"Msftedit.dll");
  HWND dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_CLIENT_NOTES),g_hGui,ClientNotesDlgProc,reinterpret_cast<LPARAM>(state));
  Check(dialog!=nullptr,"Production notes dialog created"); g_hNotesWindow=dialog;if(backgroundNotesTests) NotesBackgroundAudit("production notes dialog creation");return dialog;
}
static ClientNotesDialogState &State(HWND dialog) {
  return *reinterpret_cast<ClientNotesDialogState *>(GetWindowLongPtrW(dialog,DWLP_USER));
}
static HWND Edit(HWND dialog) { return GetDlgItem(dialog,IDC_CLIENT_NOTES_TEXT); }
static void Select(HWND edit,LONG a,LONG b) { CHARRANGE r{a,b}; SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&r)); }
static void Type(HWND dialog,const wchar_t *text) {
  SendMessageW(Edit(dialog),EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(text));
  MarkNotesChanged(dialog,State(dialog));
}
static void Command(HWND dialog,UINT command) { SendMessageW(dialog,WM_COMMAND,command,0); }
static void Save(HWND dialog) { Check(SaveClientNotesDraft(dialog,State(dialog),true),"Notebook saved safely"); }
struct ResizePaintCounts { unsigned paints=0, streams=0, ownerDraws=0; };
static LRESULT CALLBACK ObserveResize(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
  auto &counts=*reinterpret_cast<ResizePaintCounts *>(data);
  if (message==WM_PAINT) ++counts.paints;
  if (message==WM_DRAWITEM) ++counts.ownerDraws;
  if (message==EM_STREAMIN || message==EM_STREAMOUT) ++counts.streams;
  if (message==WM_NCDESTROY) RemoveWindowSubclass(window,ObserveResize,id);
  return DefSubclassProc(window,message,w,l);
}
static COLORREF OverflowPixel(HWND arrows) {
  RECT area{}; GetClientRect(arrows,&area);
  HDC screen=GetDC(arrows), dc=CreateCompatibleDC(screen);
  HBITMAP bitmap=CreateCompatibleBitmap(screen,area.right,area.bottom);
  HGDIOBJ old=SelectObject(dc,bitmap);
  SendMessageW(arrows,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
  const COLORREF color=GetPixel(dc,4,area.bottom/2);
  SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(arrows,screen);
  return color;
}
static LRESULT CALLBACK ObserveNativeFrame(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
  if (message==WM_NCPAINT || message==WM_NCACTIVATE) ++*reinterpret_cast<unsigned *>(data);
  return DefSubclassProc(window,message,w,l);
}
static void FrameRegression(HWND dialog) {
  BOOL dwmFrame=FALSE;
  Check(SUCCEEDED(DwmGetWindowAttribute(dialog,DWMWA_NCRENDERING_ENABLED,&dwmFrame,sizeof(dwmFrame))) && dwmFrame,"Notes retains DWM instead of falling back to a classic white thick frame");
  unsigned nativePaints=0;
  // Put the observer below the production subclass to detect frame requests
  // that escape to the default Windows/dialog painter.
  RemoveWindowSubclass(dialog,NotesWindowSubclass,1);
  SetWindowSubclass(dialog,ObserveNativeFrame,98,reinterpret_cast<DWORD_PTR>(&nativePaints));
  SetWindowSubclass(dialog,NotesWindowSubclass,1,0);
  SendMessageW(dialog,WM_NCPAINT,1,0);
  Check(nativePaints==0,"Custom notes frame prevents classic frame repaint from reaching default painter");
  Check(SendMessageW(dialog,WM_NCACTIVATE,FALSE,0)!=0,"Notes can lose activation without painting a native border");
  SendMessageW(dialog,WM_NCACTIVATE,TRUE,0);
  Check(nativePaints==0,"Activation changes never reach the default native frame handler");
  Check((GetWindowLongPtrW(dialog,GWL_STYLE)&WS_THICKFRAME)!=0,"Notes retains the native style required for real corner resizing");
  RemoveWindowSubclass(dialog,ObserveNativeFrame,98);
  RECT bounds{}; GetWindowRect(dialog,&bounds);
  NCCALCSIZE_PARAMS size{}; size.rgrc[0]=size.rgrc[1]=size.rgrc[2]=bounds;
  WINDOWPOS position{dialog,nullptr,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,0}; size.lppos=&position;
  Check(SendMessageW(dialog,WM_NCCALCSIZE,TRUE,reinterpret_cast<LPARAM>(&size))==0 && EqualRect(&size.rgrc[0],&bounds),"Resize preserves client pixels rather than requesting full hierarchy redraw");
  auto hit=[&](int x,int y) { return SendMessageW(dialog,WM_NCHITTEST,0,MAKELPARAM(x,y)); };
  Check(hit(bounds.left+2,bounds.top+2)==HTTOPLEFT && hit(bounds.right-2,bounds.bottom-2)==HTBOTTOMRIGHT,"Custom frame keeps both native corner resize handles");
  Check(hit(bounds.left+300,bounds.top+ScaleByDpi(38,GetDpiForWindow(dialog)))==HTCAPTION,"Empty custom header space remains draggable");
  RECT close{}; GetWindowRect(GetDlgItem(dialog,IDCANCEL),&close);
  Check(hit((close.left+close.right)/2,(close.top+close.bottom)/2)==HTCLIENT,"Close button stays clickable inside custom caption");
  for (int control : {IDC_NOTES_SWITCH_CLIENT,IDC_NOTES_LIBRARY,IDC_NOTES_TRASH,IDC_NOTES_MORE,IDC_NOTES_HELP}) {
    RECT button{};GetWindowRect(GetDlgItem(dialog,control),&button);
    Check(hit((button.left+button.right)/2,(button.top+button.bottom)/2)==HTCLIENT,"Notes header action receives mouse clicks instead of window dragging");
  }
}
static void ResizeAndOverflow(HWND dialog) {
  auto &s=State(dialog);
  for (int i=0;i<10;++i) Command(dialog,IDC_NOTES_TEMPLATE+3);
  HWND tabs=GetDlgItem(dialog,IDC_NOTES_TABS);
  HWND arrows=FindWindowExW(tabs,nullptr,UPDOWN_CLASSW,nullptr);
  DWORD_PTR data=0;
  Check(arrows && (GetWindowLongPtrW(arrows,GWL_STYLE)&WS_VISIBLE) && GetWindowSubclass(arrows,NotesOverflowSubclass,1,&data),"Overflow child receives notes theme after tabs overflow");
  const auto dark=OverflowPixel(arrows);
  Check(dark!=CLR_INVALID && GetRValue(dark)<180 && GetGValue(dark)<180 && GetBValue(dark)<180,"Overflow paint has dark theme surface rather than white system buttons");
  RECT before{},after{}; TabCtrl_GetItemRect(tabs,0,&before);
  RECT arrowRect{}; GetClientRect(arrows,&arrowRect);
  RECT tabRect{}; GetClientRect(tabs,&tabRect);
  Check(arrowRect.bottom==tabRect.bottom-2 && arrowRect.right==ScaleByDpi(52,GetDpiForWindow(tabs)),"Overflow navigation aligns to the full tab row");
  const LPARAM left=MAKELPARAM(arrowRect.right/4,arrowRect.bottom/2);
  SendMessageW(arrows,WM_LBUTTONDOWN,MK_LBUTTON,left); SendMessageW(arrows,WM_LBUTTONUP,0,left);
  TabCtrl_GetItemRect(tabs,0,&after);
  Check(after.left>before.left,"Themed overflow left arrow still scrolls native tabs");
  const LPARAM right=MAKELPARAM(arrowRect.right*3/4,arrowRect.bottom/2);
  SendMessageW(arrows,WM_LBUTTONDOWN,MK_LBUTTON,right); SendMessageW(arrows,WM_LBUTTONUP,0,right);
  TabCtrl_GetItemRect(tabs,0,&after);
  if (after.left!=before.left) std::cout<<"Overflow return: before="<<before.left<<", after="<<after.left<<", pos="<<SendMessageW(arrows,UDM_GETPOS32,0,0)<<std::endl;
  Check(after.left==before.left,"Themed overflow right arrow returns to original tab position");
  g_bThemeIsDark=false; BuildThemeColors(false,g_themeColors); UpdateThemeBrushes(); RefreshNotesForTheme();
  Check(OverflowPixel(arrows)!=dark,"Overflow follows a live switch to light theme");
  g_bThemeIsDark=true; BuildThemeColors(true,g_themeColors); UpdateThemeBrushes(); RefreshNotesForTheme();
  RedrawWindow(dialog,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
  ResizePaintCounts counts;
  EnumChildWindows(dialog,[](HWND child,LPARAM p)->BOOL { SetWindowSubclass(child,ObserveResize,97,p); return TRUE; },reinterpret_cast<LPARAM>(&counts));
  SetWindowSubclass(dialog,ObserveResize,97,reinterpret_cast<DWORD_PTR>(&counts));
  std::string original; StreamClientNotesOut(Edit(dialog),original); counts={};
  for (int i=0;i<30;++i) SetWindowPos(dialog,nullptr,0,0,900+i*7,540+i*4,SWP_NOZORDER|SWP_NOMOVE|SWP_NOACTIVATE);
  Check(counts.paints==0,"Resize queues paints instead of forcing whole window paints inside layout");
  Check(counts.ownerDraws==0,"Resize avoids immediate owner-drawn toolbar paints too");
  Check(counts.streams==0,"Resize never reloads or saves RichEdit content");
  Check(!(GetWindowLongPtrW(dialog,GWL_EXSTYLE)&WS_EX_COMPOSITED),"Notes resizing does not composite all native child controls");
  EnumChildWindows(dialog,[](HWND child,LPARAM)->BOOL { RemoveWindowSubclass(child,ObserveResize,97); return TRUE; },0);
  RemoveWindowSubclass(dialog,ObserveResize,97);
  RedrawWindow(dialog,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
  std::string resized; StreamClientNotesOut(Edit(dialog),resized);
  Check(original==resized && HasActiveNote(s),"Overflow resize preserves active note and formatted content");
  TabCtrl_GetItemRect(tabs,0,&before);
  SetWindowPos(dialog,nullptr,0,0,2000,650,SWP_NOZORDER|SWP_NOMOVE|SWP_NOACTIVATE);
  Check(!(GetWindowLongPtrW(arrows,GWL_STYLE)&WS_VISIBLE),"Overflow hides when all tabs fit");
  SetWindowPos(dialog,nullptr,0,0,900,540,SWP_NOZORDER|SWP_NOMOVE|SWP_NOACTIVATE);
  Check((GetWindowLongPtrW(arrows,GWL_STYLE)&WS_VISIBLE) && GetWindowSubclass(arrows,NotesOverflowSubclass,1,&data),"Overflow returns themed when resized narrow again");
  RECT selectedTab{},row{},navigation{};
  const int selected=TabCtrl_GetCurSel(tabs);
  TabCtrl_GetItemRect(tabs,selected,&selectedTab);GetClientRect(tabs,&row);
  GetWindowRect(arrows,&navigation);MapWindowPoints(nullptr,tabs,reinterpret_cast<POINT *>(&navigation),2);
  std::cout<<"Active tab after wide-to-narrow resize: index="<<selected<<", bounds="<<selectedTab.left<<","<<selectedTab.right<<", row="<<row.right<<", navigation="<<navigation.left<<std::endl;
  Check(selected>=0 && selectedTab.left>=0 && selectedTab.right<=navigation.left,
        "Shrinking after all tabs fit keeps the active tab visible before the overflow arrows");
  Command(dialog,IDC_NOTES_CLOSE_ALL); Save(dialog);
}
static void Tests(HWND dialog) {
  auto &s=State(dialog);
  Check(IsWindowEnabled(g_hGui)!=FALSE,"Launcher remains enabled");
  EnableWindow(dialog,FALSE);
  Check(!PrepareNotesForClientChange(L"Workspace QA") && IsWindow(dialog),"Client operation cannot destroy notes during a nested modal");
  EnableWindow(dialog,TRUE);
  Type(dialog,L"first text"); Save(dialog); HWND first=Edit(dialog); const auto firstId=s.pages[s.activePage].id;
  Command(dialog,IDC_NOTES_ADD_TAB);
  Check(s.pages.size()==2 && s.pages[s.activePage].name==L"Untitled","Plus immediately creates unnamed tab");
  Check(Edit(dialog)!=first,"Tabs keep separate native editors");
  Command(dialog,IDC_NOTES_CLOSE_TAB);
  Check(s.pages.size()==1 && Edit(dialog)==first,"Untouched blank tab discarded");
  Check(SendMessageW(first,EM_CANUNDO,0,0)!=0,"Undo survives tab switch");
  SendMessageW(first,EM_UNDO,0,0);
  Check(NotesWindowText(first).empty(),"Undo changes only original note");
  SendMessageW(first,EM_REDO,0,0); Save(dialog);
  Command(dialog,IDC_NOTES_CLOSE_TAB);
  Check(!HasActiveNote(s) && s.pages.size()==1 && !s.pages[0].open,"Close preserves saved note with zero open tabs");
  Check(!IsWindow(first),"Closed editor releases native resources");
  Command(dialog,IDC_NOTES_REOPEN);
  first=Edit(dialog);
  Check(first && NotesWindowText(first)==L"first text","Reopen restores saved content in a new editor");
  Command(dialog,IDC_NOTES_DUPLICATE);
  Check(s.pages.size()==2 && s.pages[s.activePage].id!=firstId && NotesWindowText(Edit(dialog))==L"first text","Duplicate gets distinct identity and same content");
  Command(dialog,IDC_NOTES_PIN);
  Check(s.visiblePages.front()==s.activePage,"Pinned tab moves to front");
  Command(dialog,IDC_NOTES_ARCHIVE);
  Check(s.pages[1].archived && !s.pages[1].open,"Archive hides tab without discarding note");
  NotesLibrary library; library.owner=dialog; library.state=&s;
  HWND list=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),dialog,NotesLibraryProc,reinterpret_cast<LPARAM>(&library));
  SendDlgItemMessageW(list,IDC_NOTES_FILTER,CB_SETCURSEL,2,0); NotesLibraryFill(list,library);
  Check(library.results.size()==1 && library.results[0]==1,"Archive library filter");
  SendMessageW(list,WM_COMMAND,IDC_NOTES_RESTORE,0);
  Check(!s.pages[1].archived && !s.pages[1].open,"Restored note remains available without forced tab");
  DestroyWindow(list);
  Command(dialog,IDC_NOTES_DELETE);
  Check(s.pages[0].deleted && !HasActiveNote(s),"Deletion goes to recently deleted");
  list=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),dialog,NotesLibraryProc,reinterpret_cast<LPARAM>(&library));
  SendDlgItemMessageW(list,IDC_NOTES_FILTER,CB_SETCURSEL,3,0); NotesLibraryFill(list,library);
  Check(library.results.size()==1,"Recently deleted filter");
  SendMessageW(list,WM_COMMAND,IDC_NOTES_RESTORE,0); DestroyWindow(list);
  s.pages[0].open=true; s.activePage=0; s.metadataChanged=true; NotesSelectAvailable(dialog,s);
  Select(Edit(dialog),0,-1); Type(dialog,L"alpha beta alpha"); Save(dialog);
  SetDlgItemTextW(dialog,IDC_NOTES_FIND,L"beta"); Select(Edit(dialog),0,0); NotesFind(dialog,s,false);
  CHARRANGE r{}; NotesSelection(Edit(dialog),r); Check(r.cpMin==6 && r.cpMax==10,"Find selects matching range");
  SetDlgItemTextW(dialog,IDC_NOTES_FIND,L"alpha"); Command(dialog,IDC_NOTES_REPLACE);
  SetDlgItemTextW(dialog,IDC_NOTES_REPLACE_TEXT,L"gamma"); Command(dialog,IDC_NOTES_REPLACE_ALL);
  Check(NotesWindowText(Edit(dialog))==L"gamma beta gamma","Replace all edits both matches");
  SendMessageW(Edit(dialog),EM_UNDO,0,0);
  Check(NotesWindowText(Edit(dialog))==L"alpha beta alpha","Replace all is one undo action");
  Select(Edit(dialog),0,5); inputs.push_back({L"Support",L"https://example.com/one"}); Command(dialog,IDC_NOTES_LINK);
  Check(s.pages[0].links.size()==1 && NotesRangeText(Edit(dialog),0,7)==L"Support","Labeled link inserted");
  inputs.push_back({L"Support",L"https://example.com/two"}); Command(dialog,IDC_NOTES_LINK);
  Check(s.pages[0].links[0].second==L"https://example.com/two","Existing labeled link address edited");
  Save(dialog); std::string before; StreamClientNotesOut(Edit(dialog),before);
  Command(dialog,IDC_NOTES_ZOOM_IN); std::string after; StreamClientNotesOut(Edit(dialog),after);
  Check(before==after && s.pages[0].zoom==110,"Zoom preserves document formatting");
  Select(Edit(dialog),0,-1); Type(dialog,L"\u2611\u2003Finished"); Select(Edit(dialog),10,10);
  Check(NotesListKey(Edit(dialog),WM_CHAR,VK_RETURN),"Checklist Enter handled");
  Check(NotesWindowText(Edit(dialog)).find(L"\u2610\u2003")!=std::wstring::npos,"Checklist Enter creates unchecked next item");
  Save(dialog);
  const auto snapshot=client_notes::ReadNotebook(s.clientRoot);
  auto external=snapshot.pages; external[0].rtf="{\\rtf1 External update}";
  // A whole-document external replacement must also replace its current
  // occurrence metadata; old revision links still belong to their revisions.
  external[0].links.clear();external[0].linkRanges.clear();external[0].linkRangesKnown=false;
  Check(client_notes::WriteNotebook(s.clientRoot,external,snapshot),"External writer fixture committed");
  Type(dialog,L"local draft"); Check(!SaveClientNotesDraft(dialog,s,false) && s.saveFailed,"Concurrent writer prevents overwrite");
  Check(fs::exists(s.clientRoot/s.draftName),"Failed save retains recovery file");
  Check(!PrepareNotesForClientChange(L"Workspace QA") && IsWindow(dialog),"Client changes blocked while save unresolved");
  answers.push_back(IDYES); Command(dialog,IDC_NOTES_CONFLICT);
  Check(!s.saveFailed && s.pages.size()==3,"Conflict resolution keeps latest and recovered copy");
  Check(NotesPlain(s.pages[0]).find(L"External update")!=std::wstring::npos,"External content preserved");
  Check(NotesWindowText(Edit(dialog)).find(L"local draft")!=std::wstring::npos,"Local draft preserved as copy");
  inputs.push_back({L"Destination QA",L""}); answers.push_back(IDOK); Command(dialog,IDC_NOTES_TRANSFER);
  const auto destination=client_notes::ReadNotebook(g_sDataDir/L"Sites/Destination QA");
  Check(destination.status==client_notes::ReadStatus::Ok && destination.pages.size()==2,"Copy writes destination notebook");
  Check(s.pages.size()==3 && !s.pages[s.activePage].deleted,"Copy leaves source intact");
  Save(dialog); Command(dialog,IDC_NOTES_CLOSE_ALL);
  Check(!HasActiveNote(s),"Close all produces empty state"); Save(dialog);
}
static void Extended(HWND dialog) {
  auto &s=State(dialog);
  Command(dialog,IDC_NOTES_TEMPLATE+2);
  Check(NotesWindowText(Edit(dialog)).find(L"Maintenance checklist")!=std::wstring::npos,"Maintenance template populates document");
  Type(dialog,L" keyboard edit");
  SendMessageW(dialog,WM_TIMER,1,0);
  Check(NotesPlain(client_notes::ReadNotebook(s.clientRoot).pages.back()).find(L"keyboard edit")!=std::wstring::npos,"Autosave timer commits pending edit");
  HWND editor=Edit(dialog); Select(editor,0,5);
  const LRESULT canUndo=SendMessageW(editor,EM_CANUNDO,0,0);
  g_bThemeIsDark=false; BuildThemeColors(false,g_themeColors); UpdateThemeBrushes(); RefreshNotesForTheme();
  CHARFORMAT2W format{sizeof(format)}; SendMessageW(editor,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
  Check(format.crTextColor==g_themeColors.crControlText && SendMessageW(editor,EM_CANUNDO,0,0)==canUndo,"Live theme updates text while preserving undo");
  g_bThemeIsDark=true; BuildThemeColors(true,g_themeColors); UpdateThemeBrushes(); RefreshNotesForTheme();
  const auto text=NotesWindowText(editor); std::string rtf; StreamClientNotesOut(editor,rtf);
  for (int width : {900,1100,1400,950}) {
    SetWindowPos(dialog,nullptr,0,0,width,600,SWP_NOZORDER|SWP_NOMOVE|SWP_NOACTIVATE);
    LayoutClientNotes(dialog,GetDpiForWindow(dialog));
  }
  std::string resized; StreamClientNotesOut(editor,resized);
  Check(text==NotesWindowText(editor) && rtf==resized,"Repeated resize preserves text and rich formatting");
  RECT outer{},inner{}; GetWindowRect(dialog,&outer); GetClientRect(dialog,&inner);
  Check(outer.right-outer.left==inner.right && outer.bottom-outer.top==inner.bottom,"Custom client frame occupies full window without native border");
  Check(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_STATUS)).empty() &&
      !(GetWindowLongPtrW(GetDlgItem(dialog,IDC_NOTES_STATUS),GWL_STYLE)&WS_VISIBLE),
      "Successful autosave has no visible label and cannot clip the editor's top border");
  Select(editor,0,5); Command(dialog,IDC_NOTES_HIGHLIGHT);
  SendMessageW(editor,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
  Check(!(format.dwEffects&CFE_AUTOBACKCOLOR) && format.crBackColor==s.highlightColor,"Highlight applies current swatch");
  Command(dialog,IDC_NOTES_HIGHLIGHT);
  SendMessageW(editor,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
  Check((format.dwEffects&CFE_AUTOBACKCOLOR)!=0,"Highlight toggles off");
  Save(dialog);
  const auto exportPath=g_sDataDir/L"export.rtf"; files.push_back(exportPath.wstring()); Command(dialog,IDC_NOTES_EXPORT);
  const auto exported=client_notes::ReadFileSnapshot(g_sDataDir,L"export.rtf",client_notes::kMaxRichBytes);
  Check(exported.status==client_notes::ReadStatus::Ok && client_notes::ValidRichText(exported.bytes),"RTF export writes validated rich content");
  size_t count=s.pages.size(); files.push_back(exportPath.wstring()); answers.push_back(IDOK); Command(dialog,IDC_NOTES_IMPORT);
  Check(s.pages.size()==count+1 && NotesWindowText(Edit(dialog))==text,"RTF import preview adds independent note");
  const auto txt=g_sDataDir/L"export.txt"; files.push_back(txt.wstring()); Command(dialog,IDC_NOTES_EXPORT);
  auto bytes=client_notes::ReadFileSnapshot(g_sDataDir,L"export.txt",client_notes::kMaxBytes); std::wstring decoded;
  Check(client_notes::DecodeUtf8(bytes.bytes,decoded) && decoded==text,"Text export preserves readable content");
  files.push_back(txt.wstring()); answers.push_back(IDCANCEL); count=s.pages.size(); Command(dialog,IDC_NOTES_IMPORT);
  Check(s.pages.size()==count,"Cancelled import changes nothing");
  auto drafts=s.pages; drafts.back().rtf="{\\rtf1 Crash recovery content}";
  std::string draftBytes; Check(client_notes::SerializeNotebook(drafts,draftBytes),"Crash snapshot serializes");
  Check(client_notes::WriteAuxiliary(s.clientRoot,L"ctSpaces-notes-draft-4294967294.ctn",draftBytes),"Orphan recovery fixture written");
  answers.push_back(IDYES); NotesRecoverDrafts(dialog,s);
  Check(NotesWindowText(Edit(dialog))==L"Crash recovery content" && s.pages.size()==count+1,"Orphan crash draft recovered as separate note");
  Check(!fs::exists(s.clientRoot/L"ctSpaces-notes-draft-4294967294.ctn"),"Recovered snapshot removed after successful save");
  const auto history=std::string("{\\rtf1 Prior revision}");
  client_notes::RememberRevision(s.pages[s.activePage],history,true);
  NotesLibrary lib; lib.owner=dialog; lib.state=&s; lib.history=true; lib.page=s.activePage;
  HWND preview=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),dialog,NotesLibraryProc,reinterpret_cast<LPARAM>(&lib));
  CHARFORMAT2W previewFormat{sizeof(previewFormat)}; Select(GetDlgItem(preview,IDC_NOTES_PREVIEW),0,5);
  SendDlgItemMessageW(preview,IDC_NOTES_PREVIEW,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&previewFormat));
  Check(previewFormat.crTextColor==g_themeColors.crControlText,"Read-only history preview has readable theme text");
  SendMessageW(preview,WM_COMMAND,IDOK,0); DestroyWindow(preview);
  Check(NotesWindowText(Edit(dialog))==L"Prior revision","History preview restores selected revision");
  Check(std::any_of(s.pages[s.activePage].history.begin(),s.pages[s.activePage].history.end(),[](const auto &v){return v.rtf.find("Crash recovery content")!=std::string::npos;}),"History restore retains replaced content");
  inputs.push_back({L"Destination QA",L""}); answers.push_back(IDOK); const auto moved=s.activePage; Command(dialog,IDC_NOTES_MOVE);
  Check(s.pages[moved].deleted && client_notes::ReadNotebook(g_sDataDir/L"Sites/Destination QA").pages.size()==3,"Move commits destination then retains source in trash");
  lib=NotesLibrary{}; lib.owner=dialog; lib.state=&s;
  preview=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),dialog,NotesLibraryProc,reinterpret_cast<LPARAM>(&lib));
  SendDlgItemMessageW(preview,IDC_NOTES_FILTER,CB_SETCURSEL,3,0); NotesLibraryFill(preview,lib);
  count=s.pages.size(); answers.push_back(IDNO); SendMessageW(preview,WM_COMMAND,IDC_NOTES_DELETE,0);
  Check(s.pages.size()==count,"Permanent deletion cancellation preserves note");
  answers.push_back(IDYES); SendMessageW(preview,WM_COMMAND,IDC_NOTES_DELETE,0);
  Check(s.pages.size()==count-1,"Confirmed permanent deletion removes only selected trash note"); DestroyWindow(preview);
  const auto notebook=g_sDataDir/L"export.ctn"; files.push_back(notebook.wstring()); Command(dialog,IDC_NOTES_EXPORT_ALL);
  const auto notebookBytes=client_notes::ReadFileSnapshot(g_sDataDir,L"export.ctn",client_notes::kMaxNotebookBytes);std::vector<client_notes::NotePage> notebookPages;
  Check(files.empty() && notebookBytes.status==client_notes::ReadStatus::Ok && client_notes::ParseNotebook(notebookBytes.bytes,notebookPages) && notebookPages.size()==s.pages.size(),"Notebook export produces a valid complete notebook and consumes its selected path");
  count=s.pages.size(); files.push_back(notebook.wstring()); answers.push_back(IDOK); Command(dialog,IDC_NOTES_IMPORT);
  Check(files.empty() && answers.empty() && !s.saveFailed && !s.editorLoadFailed,"Notebook import consumes its picker and preview confirmation and completes without a save or editor error");
  std::set<std::wstring> importedNames;for(const auto &page:s.pages) importedNames.insert(page.name);
  Check(s.pages.size()==count*2 && importedNames.size()==s.pages.size() && client_notes::ReadNotebook(s.clientRoot).pages.size()==s.pages.size(),"Notebook export imports and saves every uniquely named copy");
  const auto saved=client_notes::ReadNotebook(s.clientRoot);
  auto other=saved.pages; other[0].ticket=L"external";
  Check(client_notes::WriteNotebook(s.clientRoot,other,saved),"External metadata update fixture");
  s.pages[s.activePage].ticket=L"local ticket only"; s.metadataChanged=true;
  Check(!SaveClientNotesDraft(dialog,s,false),"Metadata-only conflict blocks stale save");
  answers.push_back(IDYES); Command(dialog,IDC_NOTES_CONFLICT);
  Check(s.pages[s.activePage].ticket==L"local ticket only" && !s.saveFailed,"Metadata-only draft survives conflict recovery");
  // Simulate the native tab mouse messages within this process, not an external app.
  Command(dialog,IDC_NOTES_CLOSE_ALL); Command(dialog,IDC_NOTES_TEMPLATE+1); Command(dialog,IDC_NOTES_TEMPLATE+3);
  HWND tabs=GetDlgItem(dialog,IDC_NOTES_TABS); RECT a{},b{}; TabCtrl_GetItemRect(tabs,0,&a); TabCtrl_GetItemRect(tabs,1,&b);
  const auto oldFirst=s.pages[s.visiblePages[0]].id;
  SendMessageW(tabs,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(a.left+20,a.top+10));
  SendMessageW(tabs,WM_LBUTTONUP,0,MAKELPARAM(a.left+20,a.top+10));
  Check(s.dragTab==-1,"Completed click clears pending tab drag");
  SendMessageW(tabs,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(a.left+20,a.top+10));
  SendMessageW(tabs,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(b.left+20,b.top+10));
  SendMessageW(tabs,WM_LBUTTONUP,0,MAKELPARAM(b.left+20,b.top+10));
  Check(s.pages[s.visiblePages[1]].id==oldFirst,"Tab drag persists requested order");
  Command(dialog,IDC_NOTES_CLOSE_ALL); Save(dialog);
}
static void PickerAndTemplates(HWND owner) {
  HWND dialog=nullptr;
  // A destination-picker fixture must not depend on clients left by earlier
  // tests. Keep the production list assertion exact in an isolated Sites root.
  {
  struct PickerRoot {
    fs::path original=g_sDataDir;
    PickerRoot() {g_sDataDir=original/L"isolated-picker";Fixture(L"Workspace QA");Fixture(L"Destination QA");}
    ~PickerRoot() {g_sDataDir=original;}
  } root;
  NotesClientPicker picker{L"Workspace QA"};
  dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_CLIENT_PICKER),owner,NotesClientPickerProc,reinterpret_cast<LPARAM>(&picker));
  Check(dialog && picker.clients==std::vector<std::wstring>{L"Destination QA"},"Client picker lists validated destinations and excludes source");
  HWND combo=GetDlgItem(dialog,IDC_NOTES_CLIENT_PICKER);
  DWORD_PTR pickerSubclass=0;
  Check(GetWindowSubclass(combo,NotesComboSubclass,1,&pickerSubclass)!=FALSE,"Client picker uses the notes themed dropdown surface");
  SetWindowTextW(combo,L"dEsT"); SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_NOTES_CLIENT_PICKER,CBN_EDITCHANGE),0);
  Check(SendMessageW(combo,CB_GETCOUNT,0,0)==1 && NotesWindowText(combo)==L"dEsT","Client suggestions are case insensitive and preserve typed text");
  SetWindowTextW(combo,L"no match"); SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_NOTES_CLIENT_PICKER,CBN_EDITCHANGE),0);
  Check(SendMessageW(combo,CB_GETCOUNT,0,0)==0,"Client suggestions hide unmatched destinations");
  SetWindowTextW(combo,L"destination qa"); SendMessageW(dialog,WM_COMMAND,IDOK,0);
  Check(picker.selected==L"Destination QA","Client picker resolves typed names to canonical destination"); DestroyWindow(dialog);
  }
  NotesInput one{L"One field",L"Name",L"",L"",L""};
  dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_INPUT),owner,NotesInputProc,reinterpret_cast<LPARAM>(&one));
  Check(!(GetWindowLongPtrW(GetDlgItem(dialog,IDC_NOTES_INPUT2),GWL_STYLE)&WS_VISIBLE),"Single-field prompt hides unused second input");
  RECT area{},hidden{}; GetClientRect(dialog,&area); GetWindowRect(GetDlgItem(dialog,IDC_NOTES_INPUT2),&hidden); MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT *>(&hidden),2);
  HDC screen=GetDC(dialog),dc=CreateCompatibleDC(screen); HBITMAP bitmap=CreateCompatibleBitmap(screen,area.right,area.bottom); auto old=SelectObject(dc,bitmap);
  SendMessageW(dialog,WM_ERASEBKGND,reinterpret_cast<WPARAM>(dc),0);
  Check(hidden.top-1>=area.bottom || GetPixel(dc,hidden.left+20,hidden.top-1)==g_themeColors.crWindow,"Theme painter does not draw a ghost border around hidden input");
  SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(dialog,screen); DestroyWindow(dialog);
  auto &state=State(owner); Command(owner,IDC_NOTES_TEMPLATE+2); Type(owner,L" custom checklist step"); Save(owner);
  const auto sourceText=NotesWindowText(Edit(owner));
  inputs.push_back({L"My maintenance",L"A reusable checklist for our team"}); Command(owner,IDC_NOTES_SAVE_TEMPLATE);
  NotesTemplates templates{owner,&state}; Check(NotesLoadTemplates(templates) && templates.pages.size()==5,"Custom template persists alongside four described built-ins");
  Check(templates.pages.back().ticket==L"A reusable checklist for our team" && NotesPlain(templates.pages.back())==sourceText,"Template saves hint and formatted source content");
  dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_TEMPLATE_EDITOR),owner,NotesTemplateEditorProc,reinterpret_cast<LPARAM>(&templates));
  SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_SETCURSEL,4,0); SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_NOTES_RESULTS,LBN_SELCHANGE),0);
  Check(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_TEMPLATE_HINT))==templates.pages.back().ticket,"Template editor exposes the selected hint");
  g_bThemeIsDark=false; BuildThemeColors(false,g_themeColors); UpdateThemeBrushes(); RefreshNotesLibraryForTheme(dialog);
  Check(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_PREVIEW))==sourceText,"Template theme refresh uses the correct dialog state");
  g_bThemeIsDark=true; BuildThemeColors(true,g_themeColors); UpdateThemeBrushes(); RefreshNotesLibraryForTheme(dialog);
  SetDlgItemTextW(dialog,IDC_NOTES_TEMPLATE_NAME,L"My revised maintenance");SetDlgItemTextW(dialog,IDC_NOTES_TEMPLATE_HINT,L"Edited hint");SendMessageW(dialog,WM_COMMAND,IDC_NOTES_RESTORE,0);
  Check(templates.pages.back().name==L"My revised maintenance" && templates.pages.back().ticket==L"Edited hint","Template name and hint can be edited");
  const auto stale=templates;
  SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_SETSEL,-1,-1);SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L" revised template body"));
  SendMessageW(dialog,WM_COMMAND,IDC_NOTES_RESTORE,0);
  Check(NotesPlain(templates.pages.back()).find(L"revised template body")!=std::wstring::npos && NotesWindowText(Edit(owner))==sourceText,"Direct template edits save without altering the client's current note");
  auto conflict=stale; Check(!NotesSaveTemplates(owner,conflict,conflict.pages),"Stale template changes cannot overwrite newer content");
  const auto count=state.pages.size(); SendMessageW(dialog,WM_COMMAND,IDOK,0);
  Check(state.pages.size()==count+1 && state.pages.back().ticket.empty() && state.pages.back().history.empty(),"Creating from template adds independent note without leaking hint into ticket metadata");
  Check(NotesWindowText(Edit(owner))==NotesPlain(templates.pages.back()),"Template note retains updated document content");
  const auto created=NotesWindowText(Edit(owner)); answers.push_back(IDOK); SendMessageW(dialog,WM_COMMAND,IDC_NOTES_DELETE,0);
  Check(templates.pages.size()==4 && NotesWindowText(Edit(owner))==created,"Deleting a template preserves notes already created from it");
  DestroyWindow(dialog);
  NotesTemplates reloaded{owner,&state}; Check(NotesLoadTemplates(reloaded) && reloaded.pages.size()==4,"Template deletion persists across library reopening");
  Command(owner,IDC_NOTES_DELETE);
  NotesLibrary library{owner,&state}; dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&library));
  SendDlgItemMessageW(dialog,IDC_NOTES_FILTER,CB_SETCURSEL,3,0); NotesLibraryFill(dialog,library);
  Check(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_INFO)).find(L"no automatic expiry")!=std::wstring::npos,"Recently deleted preview explains indefinite retention");
  DestroyWindow(dialog); Command(owner,IDC_NOTES_CLOSE_ALL); Save(owner);
}
static void Productivity(HWND owner) {
  Command(owner,IDC_NOTES_ADD_TAB);auto &state=State(owner);
  Type(owner,L"Original work\r\nReusable block");Save(owner);
  std::vector<std::wstring> tags;
  Check(NotesParseTags(L" network, Urgent, NETWORK, ",tags) && tags.size()==2,"Tags trim and deduplicate without case sensitivity");
  Check(!NotesParseTags(std::wstring(33,L'x'),tags),"Oversize UI tag rejected");
  auto page=state.pages[state.activePage];
  HWND metadata=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_METADATA),owner,NotesMetadataProc,reinterpret_cast<LPARAM>(&page));
  SendDlgItemMessageW(metadata,IDC_NOTES_STATUS_PICK,CB_SETCURSEL,2,0);SetDlgItemTextW(metadata,IDC_NOTES_TAGS_EDIT,L"network, Urgent");SendMessageW(metadata,WM_COMMAND,IDOK,0);
  Check(page.status==2 && page.tags.size()==2,"Status dialog applies status and tags");DestroyWindow(metadata);
  state.pages[state.activePage].status=page.status;state.pages[state.activePage].tags=page.tags;state.metadataChanged=true;Save(owner);
  auto loaded=client_notes::ReadNotebook(state.clientRoot);
  Check(loaded.pages[state.activePage].status==2 && loaded.pages[state.activePage].tags==page.tags,"Status and tags persist to disk");
  const auto before=NotesWindowText(Edit(owner));Command(owner,IDC_NOTES_WORKLOG);
  const auto after=NotesWindowText(Edit(owner));
  Check(after.find(before)==0 && after.find(L"Update - ")!=std::wstring::npos,"Work log appends a dated update without replacing earlier work");
  SendMessageW(Edit(owner),EM_UNDO,0,0);Check(NotesWindowText(Edit(owner))==before,"Work log append and formatting are one undo action");SendMessageW(Edit(owner),EM_REDO,0,0);Save(owner);
  Select(Edit(owner),14,28);auto snippet=NotesSnippetFromSelection(owner,state.pages[state.activePage]);
  Check(NotesPlain(snippet)==L"Reusable block","Snippet captures only selected rich text");
  Check(snippet.status==0 && snippet.tags.empty() && snippet.ticket.empty(),"Snippet strips note status, tags and ticket metadata");
  NotesTemplates snippets{owner,&state};snippets.snippets=true;
  Check(NotesLoadTemplates(snippets) && snippets.pages.size()==4 && snippets.root.filename()==L"NotesSnippets","Snippet library is separate from templates and has useful starter blocks");
  snippet.id=client_notes::NextId(snippets.pages);snippet.name=L"Saved QA snippet";snippets.pages.push_back(snippet);
  Check(NotesSaveTemplates(owner,snippets,snippets.pages),"Custom snippets save safely");
  Select(Edit(owner),0,0);const auto count=state.pages.size();
  HWND picker=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesTemplatesProc,reinterpret_cast<LPARAM>(&snippets));
  SendDlgItemMessageW(picker,IDC_NOTES_RESULTS,LB_SETCURSEL,4,0);SendMessageW(picker,WM_COMMAND,IDOK,0);
  Check(state.pages.size()==count && NotesWindowText(Edit(owner)).find(L"Reusable block")==0,"Snippet inserts at cursor in existing note without creating a tab");DestroyWindow(picker);
  NotesActionsState actions{true};HWND action=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_ACTIONS),owner,NotesActionsProc,reinterpret_cast<LPARAM>(&actions));
  SetDlgItemTextW(action,IDC_NOTES_QUERY,L"tags");Check(actions.results.size()==1 && kNotesActions[actions.results[0]].id==IDC_NOTES_METADATA,"Actions search matches status and tags command");
  SetDlgItemTextW(action,IDC_NOTES_QUERY,L"no-such-action");Check(actions.results.empty() && !IsWindowEnabled(GetDlgItem(action,IDOK)),"Empty action search cannot invoke an unrelated command");DestroyWindow(action);
  const auto html=NotesHandoverHtml(state,{state.activePage});
  Check(html.find(L"Waiting on client")!=std::wstring::npos && html.find(L"network, Urgent")!=std::wstring::npos && html.find(L"Reusable block")!=std::wstring::npos,"Handover includes readable content, status and tags");
  Check(NotesHtmlEscape(L"<script>&\"'")==L"&lt;script&gt;&amp;&quot;&#39;","HTML handover escapes markup and attribute characters");
  files.push_back((g_sDataDir/L"handover.html").wstring());Check(NotesExportHandover(owner,state,{state.activePage}),"Handover writes a standalone HTML file");
  Check(client_notes::ReadFileSnapshot(g_sDataDir,L"handover.html",client_notes::kMaxNotebookBytes).bytes.find("<!doctype html>")==0,"Handover file has portable HTML bytes");
  NotesLibrary library{owner,&state};HWND dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&library));
  RECT heading{},queryRect{};GetWindowRect(GetDlgItem(dialog,IDC_NOTES_LIBRARY_HEADING),&heading);GetWindowRect(GetDlgItem(dialog,IDC_NOTES_QUERY),&queryRect);
  Check(heading.bottom<queryRect.top,"Library heading stays above all four search field borders");
  MenuItemData menuData;menuData.text=L"Select all visible notes";MEASUREITEMSTRUCT measure{};measure.CtlType=ODT_MENU;measure.itemData=reinterpret_cast<ULONG_PTR>(&menuData);
  Check(HandleCleanupDialogTheme(dialog,WM_MEASUREITEM,0,reinterpret_cast<LPARAM>(&measure)).value_or(FALSE) && measure.itemWidth>100 && measure.itemHeight>=20,"Library popup uses the themed menu sizing handler instead of a blank narrow box");
  SetDlgItemTextW(dialog,IDC_NOTES_TAG_FILTER,L"network");SendDlgItemMessageW(dialog,IDC_NOTES_STATUS_FILTER,CB_SETCURSEL,3,0);NotesLibraryFill(dialog,library);
  Check(library.results.size()==1 && library.results[0]==state.activePage,"All notes combines tag and status filters");
  SetDlgItemTextW(dialog,IDC_NOTES_TAG_FILTER,L"missing");NotesLibraryFill(dialog,library);Check(library.results.empty(),"Nonmatching tags give no results");
  SetDlgItemTextW(dialog,IDC_NOTES_TAG_FILTER,L"");SendDlgItemMessageW(dialog,IDC_NOTES_STATUS_FILTER,CB_SETCURSEL,0,0);SendDlgItemMessageW(dialog,IDC_NOTES_SORT,CB_SETCURSEL,2,0);NotesLibraryFill(dialog,library);
  bool sorted=true;for(size_t i=1;i<library.results.size();++i) sorted=sorted && NotesLower(state.pages[library.results[i-1]].name)<=NotesLower(state.pages[library.results[i]].name);
  Check(sorted,"Title sort orders results without reordering stored pages");
  library.selecting=true;library.selected={state.pages[state.activePage].id};NotesLibraryFill(dialog,library);
  Check(NotesLibrarySelection(dialog,library).size()==1 && !IsWindowEnabled(GetDlgItem(dialog,IDOK)),"Selection mode scopes actions to ticked notes and blocks ambiguous Open");
  SendMessageW(dialog,WM_COMMAND,IDC_NOTES_ARCHIVE,0);Check(std::any_of(state.pages.begin(),state.pages.end(),[&](const auto &p){return p.id==page.id && p.archived;}),"Bulk archive applies to selected note");
  SendDlgItemMessageW(dialog,IDC_NOTES_FILTER,CB_SETCURSEL,2,0);
  library.selected={page.id};NotesLibraryFill(dialog,library);SendMessageW(dialog,WM_COMMAND,IDC_NOTES_RESTORE,0);
  Check(std::any_of(state.pages.begin(),state.pages.end(),[&](const auto &p){return p.id==page.id && !p.archived;}),"Bulk restore returns selected archived note");DestroyWindow(dialog);
  client_notes::NotePage second{L"Bulk companion",true,"{\\rtf1 Companion}",{}};second.id=client_notes::NextId(state.pages);second.open=false;const auto secondId=second.id;state.pages.push_back(second);state.baselines.emplace_back();state.metadataChanged=true;Save(owner);
  NotesLibrary several{owner,&state};several.selecting=true;several.selected={page.id,secondId};
  dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&several));
  Check(NotesLibrarySelection(dialog,several).size()==2,"Two independently selected notes form one bulk action scope");
  SendMessageW(dialog,WM_COMMAND,IDC_NOTES_ARCHIVE,0);
  Check(std::count_if(state.pages.begin(),state.pages.end(),[&](const auto &p){return (p.id==page.id || p.id==secondId) && p.archived;})==2,"One bulk archive updates both selected notes");
  SendDlgItemMessageW(dialog,IDC_NOTES_FILTER,CB_SETCURSEL,2,0);several.selected={page.id,secondId};NotesLibraryFill(dialog,several);
  SendMessageW(dialog,WM_COMMAND,IDC_NOTES_DELETE,0);Check(std::count_if(state.pages.begin(),state.pages.end(),[&](const auto &p){return (p.id==page.id || p.id==secondId) && p.deleted;})==2,"One bulk trash operation retains both recoverable notes");
  SendDlgItemMessageW(dialog,IDC_NOTES_FILTER,CB_SETCURSEL,3,0);several.selected={page.id,secondId};NotesLibraryFill(dialog,several);
  const auto beforeDelete=state.pages.size();answers.push_back(IDNO);SendMessageW(dialog,WM_COMMAND,IDC_NOTES_DELETE,0);Check(state.pages.size()==beforeDelete,"Canceling selected permanent deletion preserves both notes");
  answers.push_back(IDYES);SendMessageW(dialog,WM_COMMAND,IDC_NOTES_DELETE,0);Check(state.pages.size()==beforeDelete-2,"One confirmed permanent delete removes both selected notes");DestroyWindow(dialog);
  const auto quickRoot=g_sDataDir/L"Sites"/L"Destination QA";NotesQuickState quick{L"Destination QA",quickRoot,client_notes::ReadNotebook(quickRoot)};quick.pages=quick.baseline.pages;
  HWND capture=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_QUICK),owner,NotesQuickProc,reinterpret_cast<LPARAM>(&quick));
  Check(capture && GetDlgItem(capture,IDC_NOTES_QUICK_TEXT),"Quick capture has an editable body");
  Check(NotesQuickSave(capture,quick) && quick.id==0,"Untouched Quick note creates no empty note");
  SetDlgItemTextW(capture,IDC_NOTES_QUICK_TEXT,L"Captured {details} \\ safely\r\nUnicode \x4e2d\x6587");
  Check(!NotesWindowText(GetDlgItem(capture,IDC_NOTES_QUICK_TEXT)).empty(),"Quick capture accepts typed text");
  const bool quickSaved=NotesQuickSave(capture,quick);
  Check(quickSaved && quick.id!=0,"Quick capture saves body without requiring a title");
  const auto quickCount=quick.pages.size();SetDlgItemTextW(capture,IDC_NOTES_QUICK_TEXT,L"Updated capture");Check(NotesQuickSave(capture,quick) && quick.pages.size()==quickCount,"Quick capture updates one stable note instead of duplicating it");
  auto stale=quick;auto changed=quick.pages;changed.back().ticket=L"external";Check(client_notes::WriteNotebook(quickRoot,changed,quick.baseline),"Quick capture conflict fixture saves competing change");
  SetDlgItemTextW(capture,IDC_NOTES_QUICK_TEXT,L"Unsaved local draft");Check(!NotesQuickSave(capture,stale) && NotesWindowText(GetDlgItem(capture,IDC_NOTES_QUICK_TEXT))==L"Unsaved local draft","Quick capture conflict preserves typed draft and refuses overwrite");DestroyWindow(capture);
  Fixture(L"Quick fresh QA");const auto freshRoot=g_sDataDir/L"Sites"/L"Quick fresh QA";NotesQuickState fresh{L"Quick fresh QA",freshRoot,client_notes::ReadNotebook(freshRoot)};fresh.pages=fresh.baseline.pages;
  capture=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_QUICK),owner,NotesQuickProc,reinterpret_cast<LPARAM>(&fresh));SetDlgItemTextW(capture,IDC_NOTES_QUICK_TEXT,L"First captured thought");
  Check(NotesQuickSave(capture,fresh) && fresh.pages.size()==1 && fresh.pages[0].name==L"Quick note" && !fresh.pages[0].open,"First capture saves exactly one closed note without an empty placeholder");DestroyWindow(capture);
  Command(owner,IDC_NOTES_CLOSE_ALL);Save(owner);
}
static void Walkthrough(HWND owner) {
  const auto &workspace=State(owner);
  const auto count=workspace.pages.size(),active=workspace.activePage;
  const auto text=NotesWindowText(Edit(owner)); const auto saved=client_notes::ReadNotebook(workspace.clientRoot);
  Command(owner,IDC_NOTES_WALKTHROUGH); Check(walkthroughOpens==1,"Walkthrough command opens the dedicated read-only guide");
  HWND overview=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_HELP),owner,NotesHelpProc,0);
  Check(GetDlgItem(overview,IDC_NOTES_WALKTHROUGH)!=nullptr,"Quick Help offers a named step-by-step walkthrough entry"); DestroyWindow(overview);
  NotesTourState tour;
  HWND dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_WALKTHROUGH),owner,NotesTourProc,reinterpret_cast<LPARAM>(&tour));
  Check(dialog && SendDlgItemMessageW(dialog,IDC_NOTES_TOUR_TOPICS,CB_GETCOUNT,0,0)==kNotesTourSteps.size(),"Walkthrough exposes every everyday-use topic");
  Check(!IsWindowEnabled(GetDlgItem(dialog,IDC_GUIDE_BACK)) && tour.position==0,"Walkthrough starts at first step with Back disabled");
  SendMessageW(dialog,WM_COMMAND,IDOK,0); Check(tour.position==1,"Next advances the walkthrough");
  SendMessageW(dialog,WM_COMMAND,IDC_GUIDE_BACK,0); SendMessageW(dialog,WM_COMMAND,IDC_GUIDE_BACK,0);
  Check(tour.position==0,"Back returns to first step and cannot underflow");
  bool images=true,steps=true;
  for (size_t i=0;i<kNotesTourSteps.size();++i) {
    SendDlgItemMessageW(dialog,IDC_NOTES_TOUR_TOPICS,CB_SETCURSEL,i,0);
    SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_NOTES_TOUR_TOPICS,CBN_SELCHANGE),0);
    images=images && tour.image && tour.image->GetWidth()>0 && tour.image->GetHeight()>0;
    steps=steps && tour.position==i && NotesWindowText(GetDlgItem(dialog,IDC_GUIDE_TITLE))==kNotesTourSteps[i].title;
  }
  Check(images,"Every walkthrough topic decodes its embedded real screenshot");
  Check(steps,"Topic selector reaches every step and updates its title");
  Check(NotesWindowText(GetDlgItem(dialog,IDOK))==L"Finish","Last step changes Next to Finish");
  SendDlgItemMessageW(dialog,IDC_NOTES_TOUR_TOPICS,CB_SETCURSEL,8,0);
  SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_NOTES_TOUR_TOPICS,CBN_SELCHANGE),0);
  const auto warning=NotesWindowText(GetDlgItem(dialog,IDC_NOTES_TOUR_WARNING));
  const auto body=NotesWindowText(GetDlgItem(dialog,IDC_GUIDE_BODY));
  Check(warning.find(L"PERMANENT DELETION")!=std::wstring::npos && warning.find(L"cannot be undone")!=std::wstring::npos &&
      body.find(L"hidden by a search")!=std::wstring::npos && body.find(L"revision histories")!=std::wstring::npos,"Permanent deletion step warns about history, hidden results and no undo");
  Check((GetWindowLongPtrW(GetDlgItem(dialog,IDC_GUIDE_BODY),GWL_STYLE)&ES_READONLY)!=0 && GetDlgItem(dialog,IDC_NOTES_EMPTY_TRASH)==nullptr,"Walkthrough text is read-only and has no deletion command control");
  SendDlgItemMessageW(dialog,IDC_NOTES_TOUR_TOPICS,CB_SETCURSEL,CB_ERR,0); SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_NOTES_TOUR_TOPICS,CBN_SELCHANGE),0);
  Check(tour.position==8,"Invalid topic selection preserves the current step");
  MINMAXINFO limits{}; SendMessageW(dialog,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&limits));
  SetWindowPos(dialog,nullptr,0,0,limits.ptMinTrackSize.x,limits.ptMinTrackSize.y,SWP_NOMOVE|SWP_NOZORDER);
  RECT content{},picture{},caption{},next{},close{};
  GetWindowRect(GetDlgItem(dialog,IDC_GUIDE_BODY),&content); GetWindowRect(GetDlgItem(dialog,IDC_NOTES_TOUR_IMAGE),&picture);
  GetWindowRect(GetDlgItem(dialog,IDC_NOTES_TOUR_CAPTION),&caption); GetWindowRect(GetDlgItem(dialog,IDOK),&next); GetWindowRect(GetDlgItem(dialog,IDCANCEL),&close);
  Check(content.right<picture.left && picture.bottom<caption.top && caption.bottom<next.top && next.right<close.left,"Walkthrough content, screenshot, caption and buttons remain separate at minimum size");
  DestroyWindow(dialog);
  Check(workspace.pages.size()==count && workspace.activePage==active && NotesWindowText(Edit(owner))==text &&
      client_notes::ReadNotebook(workspace.clientRoot).notebook_snapshot.bytes==saved.notebook_snapshot.bytes,"Reading all walkthrough steps leaves notebook and editor unchanged");
}
static void GuidanceAndBulkTrash(HWND owner) {
  auto &state=State(owner);
  Command(owner,IDC_NOTES_ADD_TAB); Type(owner,L"Ticket investigation for the current client");
  inputs.push_back({L"44553",L"https://example.com/ticket/44553"}); Command(owner,IDC_NOTES_TICKET);
  const auto activeId=state.pages[state.activePage].id;
  NotesLibrary library{owner,&state};
  HWND dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&library));
  const LRESULT selected=SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_GETCURSEL,0,0);
  Check(selected>=0 && library.results[selected]==state.activePage,"All notes starts with current note selected");
  wchar_t label[1024]{}; SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_GETTEXT,selected,reinterpret_cast<LPARAM>(label));
  Check(std::wstring(label).find(L"[Ticket 44553]")!=std::wstring::npos,"Ticket number is visible directly in All notes result list");
  Check(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_INFO)).find(L"\r\nTicket: 44553")!=std::wstring::npos,"Ticket reference gets its own line in preview information");
  Check(IsWindowEnabled(GetDlgItem(dialog,IDC_NOTES_OPEN_TICKET)),"Valid ticket URL enables a labeled action beside preview");
  Check(!(GetWindowLongPtrW(GetDlgItem(dialog,IDC_NOTES_EMPTY_TRASH),GWL_STYLE)&WS_VISIBLE),"Empty trash is hidden outside Recently deleted");
  SetDlgItemTextW(dialog,IDC_NOTES_QUERY,L"44553"); NotesLibraryFill(dialog,library);
  Check(library.results.size()==1 && state.pages[library.results[0]].id==activeId,"Ticket number search resolves the linked note");
  DestroyWindow(dialog);
  NotesInput input{L"Ticket details",L"Ticket number",L"Ticket URL",L"44553",L"",L"The number appears in All notes. A URL enables Open ticket link."};
  dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_INPUT),owner,NotesInputProc,reinterpret_cast<LPARAM>(&input));
  RECT hint{},field{}; GetWindowRect(GetDlgItem(dialog,IDC_NOTES_INPUT_HINT),&hint); GetWindowRect(GetDlgItem(dialog,IDC_NOTES_LABEL1),&field);
  Check((GetWindowLongPtrW(GetDlgItem(dialog,IDC_NOTES_INPUT_HINT),GWL_STYLE)&WS_VISIBLE) && hint.bottom<=field.top,"Ticket dialog explains fields above the inputs without overlap"); DestroyWindow(dialog);
  Check(GetDlgItem(owner,IDC_NOTES_HELP)!=nullptr && (GetWindowLongPtrW(GetDlgItem(owner,IDC_NOTES_HELP),GWL_STYLE)&WS_VISIBLE),"Notes keeps a discoverable Help button");
  Command(owner,IDC_NOTES_HELP); Check(GetPrivateProfileIntW(L"notes_window",L"help_seen",0,g_sConfigPath.c_str())==1,"Reading quick guide persists first-use dismissal");
  dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_HELP),owner,NotesHelpProc,0);
  Check(dialog && GetDlgItem(dialog,IDC_NOTES_TEMPLATES) && GetDlgItem(dialog,IDC_NOTES_LIBRARY),"Quick guide includes labeled routes to templates and All notes"); DestroyWindow(dialog);
  client_notes::NotePage archived{L"Archived survivor",true,"{\\rtf1\\ansi Archived content}",{}};
  archived.id=client_notes::NextId(state.pages); archived.archived=true; archived.open=false;
  const auto archivedId=archived.id; state.pages.push_back(std::move(archived)); state.baselines.emplace_back();
  for (int i=0;i<100;++i) {
    client_notes::NotePage deleted{L"Deleted item "+std::to_wstring(i),true,"{\\rtf1\\ansi Trash content}",{}};
    deleted.id=client_notes::NextId(state.pages); deleted.deleted=true; deleted.open=false;
    deleted.history.push_back({client_notes::Now(),"{\\rtf1\\ansi Old trash content}"});
    state.pages.push_back(std::move(deleted)); state.baselines.emplace_back();
  }
  state.metadataChanged=true; Save(owner);
  const auto before=state.pages.size(),trash=static_cast<size_t>(std::count_if(state.pages.begin(),state.pages.end(),[](const auto &p){return p.deleted;}));
  const auto destination=client_notes::ReadNotebook(g_sDataDir/L"Sites/Destination QA");
  dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&library));
  SendMessageW(dialog,WM_COMMAND,IDC_NOTES_EMPTY_TRASH,0); Check(state.pages.size()==before,"Bulk deletion cannot run from Current notes view");
  SendDlgItemMessageW(dialog,IDC_NOTES_FILTER,CB_SETCURSEL,3,0); SetDlgItemTextW(dialog,IDC_NOTES_QUERY,L"Deleted item 99"); NotesLibraryFill(dialog,library);
  Check(library.results.size()==1 && IsWindowEnabled(GetDlgItem(dialog,IDC_NOTES_EMPTY_TRASH)),"Empty trash is available even with only one matching search result");
  answers.push_back(IDNO); SendMessageW(dialog,WM_COMMAND,IDC_NOTES_EMPTY_TRASH,0);
  Check(state.pages.size()==before && lastNotice.find(std::to_wstring(trash))!=std::wstring::npos && lastNotice.find(L"hidden by the current search")!=std::wstring::npos,"Bulk trash confirmation states total scope and cancel preserves every note");
  library.readOnly=true; SendMessageW(dialog,WM_COMMAND,IDC_NOTES_EMPTY_TRASH,0); library.readOnly=false;
  Check(state.pages.size()==before,"Read-only library rejects Empty trash");
  state.metadataChanged=true;
  HANDLE locked=CreateFileW((state.clientRoot/client_notes::kNotebookFileName).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  Check(locked!=INVALID_HANDLE_VALUE,"Notebook locked to exercise bulk save failure");
  answers.push_back(IDYES); SendMessageW(dialog,WM_COMMAND,IDC_NOTES_EMPTY_TRASH,0); CloseHandle(locked);
  Check(state.pages.size()==before && state.saveFailed,"Failed save blocks bulk deletion before any note is erased");
  Save(owner); SetDlgItemTextW(dialog,IDC_NOTES_QUERY,L"no matching results"); NotesLibraryFill(dialog,library);
  Check(library.results.empty() && IsWindowEnabled(GetDlgItem(dialog,IDC_NOTES_EMPTY_TRASH)),"Empty trash works independently of selected or visible results");
  answers.push_back(IDYES); SendMessageW(dialog,WM_COMMAND,IDC_NOTES_EMPTY_TRASH,0);
  Check(state.pages.size()==before-trash && std::none_of(state.pages.begin(),state.pages.end(),[](const auto &p){return p.deleted;}),"One confirmed operation removes all deleted notes and their histories");
  Check(std::any_of(state.pages.begin(),state.pages.end(),[&](const auto &p){return p.id==activeId;}) &&
      std::any_of(state.pages.begin(),state.pages.end(),[&](const auto &p){return p.id==archivedId && p.archived;}),"Empty trash preserves current and archived notes");
  const auto saved=client_notes::ReadNotebook(state.clientRoot);
  Check(saved.pages.size()==state.pages.size() && std::none_of(saved.pages.begin(),saved.pages.end(),[](const auto &p){return p.deleted;}),"Emptied trash persists in the saved notebook");
  Check(client_notes::ReadNotebook(g_sDataDir/L"Sites/Destination QA").notebook_snapshot.bytes==destination.notebook_snapshot.bytes,"Empty trash affects only the selected client's notebook");
  Check(!IsWindowEnabled(GetDlgItem(dialog,IDC_NOTES_EMPTY_TRASH)),"Empty trash disables when nothing remains deleted");
  Check(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_DELETE))==L"Delete forever","Empty trash view keeps the permanent deletion label");
  MINMAXINFO libraryLimits{}; SendMessageW(dialog,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&libraryLimits));
  SetWindowPos(dialog,nullptr,0,0,libraryLimits.ptMinTrackSize.x,libraryLimits.ptMinTrackSize.y,SWP_NOMOVE|SWP_NOZORDER);
  RECT trashButton{},closeButton{};
  GetWindowRect(GetDlgItem(dialog,IDC_NOTES_EMPTY_TRASH),&trashButton); GetWindowRect(GetDlgItem(dialog,IDCANCEL),&closeButton);
  Check(trashButton.right<closeButton.left,"Empty trash and Close do not overlap at the minimum library size");
  DestroyWindow(dialog); Command(owner,IDC_NOTES_CLOSE_ALL); Save(owner);
}
static void TicketCodeAndTopActions(HWND owner) {
  Command(owner,IDC_NOTES_ADD_TAB);auto &state=State(owner);
  Type(owner,L"Before code\r\nSELECTED\r\nAfter code");Save(owner);
  auto &page=state.pages[state.activePage];page.ticket=L"44553";page.ticketUrl=L"https://example.com/tickets/44553";state.metadataChanged=true;Save(owner);
  auto count=shellOpens;answers.push_back(IDNO);Command(owner,IDC_NOTES_OPEN_TICKET);
  Check(shellOpens==count && lastNotice.find(L"default browser")!=std::wstring::npos,"Ticket confirmation explains regular default browser session and cancel does not launch");
  answers.push_back(IDYES);Command(owner,IDC_NOTES_OPEN_TICKET);
  Check(shellOpens==count+1 && shellTarget==page.ticketUrl,"Editor ticket opens URL with Windows association and no client profile arguments");
  answers.push_back(IDYES);OpenNotesUrl(owner,state,page.ticketUrl);
  Check(shellOpens==count+2,"Ticket URL inside note body follows default browser route");
  OpenNotesUrl(owner,state,L"file:///unsafe",true);Check(shellOpens==count+2,"Ticket browser route rejects non-web URLs");
  shellResult=31;answers.push_back(IDYES);Command(owner,IDC_NOTES_OPEN_TICKET);shellResult=33;
  Check(lastNotice.find(L"Windows could not open")!=std::wstring::npos,"Default browser launch failure is explained");
  NotesLibrary lib{owner,&state};HWND library=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&lib));
  for (size_t i=0;i<lib.results.size();++i) if (lib.results[i]==state.activePage) SendDlgItemMessageW(library,IDC_NOTES_RESULTS,LB_SETCURSEL,i,0);
  answers.push_back(IDYES);SendMessageW(library,WM_COMMAND,IDC_NOTES_OPEN_TICKET,0);
  Check(shellTarget==page.ticketUrl && shellOpens==count+4,"All notes ticket uses the same default-browser route");DestroyWindow(library);
  Command(owner,IDC_NOTES_DETAILS);Check(state.panel && IsWindow(state.panel),"Details opens the single metadata editing panel");CloseNotesPanel(state);
  page.status=3;UpdateNotesToolbar(owner,state);
  Check(NotesWindowText(GetDlgItem(owner,IDC_NOTES_WORK_STATUS))==L"Follow-up","Read-only status indicator displays current local ticket-work status");
  Command(owner,IDC_NOTES_FIND_TOGGLE);Check(state.findVisible,"Top Find button exposes note search");Command(owner,IDC_NOTES_FIND_CLOSE);
  for (int width : {900,1240,1500,900}) {
    SetWindowPos(owner,nullptr,0,0,ScaleByDpi(width,GetDpiForWindow(owner)),ScaleByDpi(600,GetDpiForWindow(owner)),SWP_NOMOVE|SWP_NOZORDER);
    RECT prior{};bool first=true;for (int id : {IDC_NOTES_FIND_TOGGLE,IDC_NOTES_SNIPPETS,IDC_NOTES_TEMPLATES,IDC_NOTES_WORKLOG,IDC_NOTES_FORMAT,IDC_NOTES_WORK_STATUS,IDC_NOTES_DETAILS}) {
      RECT bounds{};GetWindowRect(GetDlgItem(owner,id),&bounds);Check((GetWindowLongPtrW(GetDlgItem(owner,id),GWL_STYLE)&WS_VISIBLE) && (first || bounds.left>=prior.right),"Document heading actions remain visible and separated across responsive layouts");prior=bounds;first=false;
    }
    RECT trash{},actions{};GetWindowRect(GetDlgItem(owner,IDC_NOTES_TRASH),&trash);GetWindowRect(GetDlgItem(owner,IDC_NOTES_MORE),&actions);
    RECT all{};GetWindowRect(GetDlgItem(owner,IDC_NOTES_LIBRARY),&all);
    Check(all.right<=trash.left && trash.right<=actions.left && (GetWindowLongPtrW(GetDlgItem(owner,IDC_NOTES_TRASH),GWL_STYLE)&WS_VISIBLE),"Direct Trash action fits beside All notes across window sizes");
    RECT format{},editor{},heading{},allNotes{};GetWindowRect(GetDlgItem(owner,IDC_NOTES_STYLE),&format);GetWindowRect(Edit(owner),&editor);
    GetWindowRect(GetDlgItem(owner,IDC_NOTES_SWITCH_CLIENT),&heading);GetWindowRect(GetDlgItem(owner,IDC_NOTES_LIBRARY),&allNotes);
    Check(prior.bottom<editor.top && !IsWindowVisible(GetDlgItem(owner,IDC_NOTES_STYLE)) && heading.right<=allNotes.left,"Document actions lead straight into the editor; formatting occupies no permanent space");
  }
  NotesTemplates templates{owner,&state};Check(NotesLoadTemplates(templates),"Templates load for top action management");
  HWND manager=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_TEMPLATE_EDITOR),owner,NotesTemplateEditorProc,reinterpret_cast<LPARAM>(&templates));
  Check((GetWindowLongPtrW(GetDlgItem(manager,IDC_NOTES_SAVE_TEMPLATE),GWL_STYLE)&WS_VISIBLE) && IsWindowEnabled(GetDlgItem(manager,IDC_NOTES_SAVE_TEMPLATE)),"Templates offers a direct From current note action");
  count=static_cast<unsigned>(templates.pages.size());SendMessageW(manager,WM_COMMAND,IDC_NOTES_SAVE_TEMPLATE,0);
  SetDlgItemTextW(manager,IDC_NOTES_TEMPLATE_NAME,L"Top row QA template");SetDlgItemTextW(manager,IDC_NOTES_TEMPLATE_HINT,L"Created from the templates manager");SendMessageW(manager,WM_COMMAND,IDC_NOTES_RESTORE,0);
  Check(templates.pages.size()==count+1 && templates.pages.back().ticket==L"Created from the templates manager","Template manager creates and reloads a new reusable template");DestroyWindow(manager);
  NotesCodeInput input{4,L"print(\"{hello}\\world\")\r\n\t# Unicode: \x4e2d\x6587"};
  HWND code=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_CODE_BLOCK),owner,NotesCodeProc,reinterpret_cast<LPARAM>(&input));
  Check(SendDlgItemMessageW(code,IDC_NOTES_CODE_LANGUAGE,CB_GETCOUNT,0,0)==std::size(kNotesCodeLanguages) && NotesWindowText(GetDlgItem(code,IDC_NOTES_CODE_SOURCE))==input.source,"Code dialog exposes language choices and editable source");DestroyWindow(code);
  const auto before=NotesWindowText(Edit(owner));Select(Edit(owner),13,21);
  Check(NotesInsertCode(owner,state,input),"Selected prose converts into a labeled native code box");
  const auto after=NotesWindowText(Edit(owner));Check(after.find(L"Python - Code")!=std::wstring::npos && after.find(L"{hello}\\world")!=std::wstring::npos && after.find(L"\x4e2d\x6587")!=std::wstring::npos && after.find(L"Before code")!=std::wstring::npos && after.find(L"After code")!=std::wstring::npos,"Code label, literal syntax, Unicode and surrounding prose survive insertion");
  const auto body=static_cast<LONG>(after.find(L"print"));Select(Edit(owner),body,body+5);
  PARAFORMAT2 paragraph{sizeof(paragraph)};SendMessageW(Edit(owner),EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
  CHARFORMAT2W format{sizeof(format)};SendMessageW(Edit(owner),EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
  Check((paragraph.wEffects&PFE_TABLE) && _wcsicmp(format.szFaceName,L"Consolas")==0 && format.crTextColor==g_themeColors.crControlText,"Code body is inside a real table box with monospace theme text");
  Check(!NotesInsertCode(owner,state,input) && NotesWindowText(Edit(owner))==after,"Nested code box insertion is blocked without changing source");
  SendMessageW(Edit(owner),EM_UNDO,0,0);Check(NotesWindowText(Edit(owner))==before,"Code conversion is one undo action");SendMessageW(Edit(owner),EM_REDO,0,0);Save(owner);
  const auto saved=client_notes::ReadNotebook(state.clientRoot);Check(saved.pages[state.activePage].rtf.find("\\trowd")!=std::string::npos && NotesPlain(saved.pages[state.activePage]).find(L"Python - Code")!=std::wstring::npos,"Code box and language survive notebook save and read");
  const auto handover=NotesHandoverHtml(state,{state.activePage});Check(handover.find(L"Python - Code")!=std::wstring::npos && handover.find(L'\v')==std::wstring::npos && handover.find(L'\a')==std::wstring::npos && handover.find(L"# Unicode")!=std::wstring::npos,"Readable handover retains code caption and portable line separators");
  g_bThemeIsDark=false;BuildThemeColors(false,g_themeColors);UpdateThemeBrushes();RefreshNotesForTheme();Select(Edit(owner),body,body+5);SendMessageW(Edit(owner),EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
  Check(format.crTextColor==g_themeColors.crControlText,"Code remains readable after changing to light theme");
  g_bThemeIsDark=true;BuildThemeColors(true,g_themeColors);UpdateThemeBrushes();RefreshNotesForTheme();
  Command(owner,IDC_NOTES_CLOSE_ALL);Save(owner);
}
static void PumpPanelMessages() {
  MSG msg{};while (PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&msg);DispatchMessageW(&msg);}
}
static void InspectorNavigation() {
  Fixture(L"Inspector A");Fixture(L"Inspector B");
  HWND owner=Open(L"Inspector A");Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Original client text");Save(owner);
  auto &state=State(owner);const auto aRoot=state.clientRoot;
  const auto pageCount=state.pages.size();
  SendMessageW(owner,WM_COMMAND,MAKEWPARAM(IDC_NOTES_ADD_TAB,BN_SETFOCUS),0);
  Check(state.pages.size()==pageCount,"Focusing New note does not create a note");
  SendMessageW(owner,WM_COMMAND,MAKEWPARAM(IDC_NOTES_SWITCH_CLIENT,BN_SETFOCUS),0);
  Check(!state.panel,"Focusing the client picker does not execute its action");
  Check(!GetDlgItem(owner,IDC_NOTES_CLIENT_HEADING) && NotesWindowText(GetDlgItem(owner,IDC_NOTES_MORE))==L"More \u2026","Notes header has one client selector, no redundant Notes label and Windows More text");
  RECT before{},after{};GetWindowRect(Edit(owner),&before);Command(owner,IDC_NOTES_DETAILS);HWND panel=state.panel;
  Check(IsWindow(panel) && IsWindowEnabled(owner),"Details is a temporary panel with an enabled owner");
  SendMessageW(owner,WM_COMMAND,MAKEWPARAM(IDC_NOTES_DETAILS,BN_KILLFOCUS),0);
  Check(IsWindow(panel) && state.panel==panel,"A panel taking focus does not dismiss or reopen itself");
  SendMessageW(owner,WM_COMMAND,MAKEWPARAM(IDC_CLIENT_NOTES_TEXT,EN_KILLFOCUS),0);
  Check(IsWindow(panel) && state.panel==panel,"Editor focus loss does not dismiss the active inspector");
  ShowWindow(panel,SW_SHOW);
  HWND statusCombo=GetDlgItem(panel,IDC_NOTES_STATUS_PICK);SetFocus(statusCombo);
  SendMessageW(statusCombo,CB_SHOWDROPDOWN,TRUE,0);
  MSG accept{};accept.hwnd=statusCombo;accept.message=WM_KEYDOWN;accept.wParam=VK_RETURN;
  if (!ProcessNotesPanelMessage(accept)) {TranslateMessage(&accept);DispatchMessageW(&accept);}
  Check(IsWindow(panel) && state.panel==panel,"Enter accepts the open status dropdown without closing Details");
  SendMessageW(statusCombo,CB_SHOWDROPDOWN,FALSE,0);
  RECT panelRect{},field{};GetClientRect(panel,&panelRect);GetWindowRect(GetDlgItem(panel,IDC_NOTES_TAGS_EDIT),&field);
  MapWindowPoints(nullptr,panel,reinterpret_cast<POINT *>(&field),2);
  HDC screen=GetDC(panel),buffer=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,panelRect.right,panelRect.bottom);auto oldBitmap=SelectObject(buffer,bitmap);
  SendMessageW(panel,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(buffer),PRF_CLIENT);
  Check(GetPixel(buffer,field.left+10,field.top-1)==GetPixel(buffer,field.left+10,field.bottom) && GetPixel(buffer,field.left+10,field.top-1)!=g_themeColors.crWindow,"Details edit fields paint their complete top and bottom borders");
  SelectObject(buffer,oldBitmap);DeleteObject(bitmap);DeleteDC(buffer);ReleaseDC(panel,screen);
  GetWindowRect(Edit(owner),&after);Check(EqualRect(&before,&after),"Details never shrinks or moves the editor");
  SetDlgItemTextW(panel,IDC_NOTES_INPUT1,L"48301");SetDlgItemTextW(panel,IDC_NOTES_INPUT2,L"https://tickets.example.com/48301");SetDlgItemTextW(panel,IDC_NOTES_TAGS_EDIT,L"network, handover");
  SendDlgItemMessageW(panel,IDC_NOTES_STATUS_PICK,CB_SETCURSEL,2,0);SendMessageW(panel,WM_TIMER,1,0);
  auto disk=client_notes::ReadNotebook(aRoot);
  Check(disk.pages[state.activePage].ticket==L"48301" && disk.pages[state.activePage].tags.size()==2 && disk.pages[state.activePage].status==2,"Unified details autosave ticket, status and tags to the original client");
  SetDlgItemTextW(panel,IDC_NOTES_INPUT2,L"file:///bad");SendMessageW(panel,WM_TIMER,1,0);
  Check(client_notes::ReadNotebook(aRoot).pages[state.activePage].ticketUrl==L"https://tickets.example.com/48301" && NotesWindowText(GetDlgItem(panel,IDC_NOTES_DETAILS_HINT)).find(L"Not saved")!=std::wstring::npos,"Invalid details show an explanation without overwriting valid metadata");
  SetDlgItemTextW(panel,IDC_NOTES_INPUT2,L"https://tickets.example.com/48301");SendMessageW(panel,WM_ACTIVATE,WA_INACTIVE,reinterpret_cast<LPARAM>(owner));PumpPanelMessages();
  Check(!IsWindow(panel) && !state.panel,"Clicking outside Details closes it and flushes pending valid changes");
  Command(owner,IDC_NOTES_DETAILS);panel=state.panel;SetDlgItemTextW(panel,IDC_NOTES_INPUT2,L"https://unfinished");
  SetDlgItemTextW(panel,IDC_NOTES_INPUT2,L"unfinished");CloseNotesPanel(state);
  Check(state.detailsDrafts.size()==1 && client_notes::ReadNotebook(aRoot).pages[state.activePage].ticketUrl==L"https://tickets.example.com/48301","Outside dismissal retains an invalid details draft without changing saved metadata");
  Command(owner,IDC_NOTES_DETAILS);panel=state.panel;
  Check(NotesWindowText(GetDlgItem(panel,IDC_NOTES_INPUT2))==L"unfinished","Reopening Details restores its unfinished invalid field");
  SetDlgItemTextW(panel,IDC_NOTES_INPUT2,L"https://tickets.example.com/48301");CloseNotesPanel(state);
  Check(state.detailsDrafts.empty(),"Correcting Details clears its retained draft");
  Command(owner,IDC_NOTES_DETAILS);panel=state.panel;const std::wstring longTicket(100,L'A');
  SetDlgItemTextW(panel,IDC_NOTES_INPUT1,longTicket.c_str());SendMessageW(panel,WM_TIMER,1,0);
  Check(client_notes::ReadNotebook(aRoot).pages[state.activePage].ticket==longTicket,"Details preserves the existing 128-character ticket-reference allowance");
  SetDlgItemTextW(panel,IDC_NOTES_INPUT1,L"48301");CloseNotesPanel(state);
  Command(owner,IDC_NOTES_MORE);panel=state.panel;
  Check(IsWindow(panel) && IsWindowEnabled(owner),"More actions is searchable in a temporary panel");
  SetDlgItemTextW(panel,IDC_NOTES_QUERY,L"handover");
  auto &actions=*reinterpret_cast<NotesPanel *>(GetWindowLongPtrW(panel,DWLP_USER));
  Check(actions.actionState.results.size()==1 && kNotesActions[actions.actionState.results.front()].id==IDC_NOTES_HANDOVER,"More action search finds a specific action and its explanation");
  SendMessageW(panel,WM_ACTIVATE,WA_INACTIVE,reinterpret_cast<LPARAM>(owner));PumpPanelMessages();
  Check(!IsWindow(panel),"Clicking outside More actions dismisses it");
  for (int i=0;i<100;++i) Fixture(L"Search fixture "+std::to_wstring(i));
  const auto pins=g_pinnedClients;g_pinnedClients={L"Inspector B"};
  Command(owner,IDC_NOTES_SWITCH_CLIENT);panel=state.panel;
  Check(IsWindow(panel) && SendDlgItemMessageW(panel,IDC_NOTES_CLIENT_MATCHES,LB_GETCOUNT,0,0)==8,"A hundred clients produces a short eight-result panel");
  auto &picker=*reinterpret_cast<NotesPanel *>(GetWindowLongPtrW(panel,DWLP_USER));
  Check(picker.matches.front()==L"Inspector B","Pinned clients appear first without scrolling");
  SetDlgItemTextW(panel,IDC_NOTES_CLIENT_SEARCH,L"Search fixture 99");
  Check(picker.matches.size()==1 && picker.matches.front()==L"Search fixture 99","Typing searches every client beyond the initial shortlist");
  SendMessageW(panel,WM_ACTIVATE,WA_INACTIVE,reinterpret_cast<LPARAM>(owner));PumpPanelMessages();
  Check(!IsWindow(panel) && state.clientName==L"Inspector A","Clicking outside Switch client closes it without switching");
  Command(owner,IDC_NOTES_SWITCH_CLIENT);panel=state.panel;MSG key{GetDlgItem(panel,IDC_NOTES_CLIENT_SEARCH),WM_KEYDOWN,VK_ESCAPE};
  Check(ProcessNotesPanelMessage(key) && !IsWindow(panel),"Escape dismisses a temporary panel");g_pinnedClients=pins;
  // Store B with the same local note ID as A: cross-client search must not use
  // local IDs as global keys or mutation targets.
  const auto bRoot=g_sDataDir/L"Sites"/L"Inspector B";
  client_notes::NotePage b{L"B independent",true,NotesPlainRtf(L"Unique B content"),{}};b.id=state.pages[state.activePage].id;b.created=b.modified=client_notes::Now();
  Check(client_notes::WriteNotebook(bRoot,{b},client_notes::ReadNotebook(bRoot)),"Independent client notebook accepts colliding local note IDs");
  const auto aBytes=client_notes::ReadNotebook(aRoot).notebook_snapshot.bytes,bBytes=client_notes::ReadNotebook(bRoot).notebook_snapshot.bytes;
  NotesLibrary lib{owner,&state};HWND manager=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&lib));
  {
    HWND scope=GetDlgItem(manager,IDC_NOTES_SCOPE);HDC screen=GetDC(scope),buffer=CreateCompatibleDC(screen);
    HBITMAP bitmap=CreateCompatibleBitmap(screen,320,32);auto old=SelectObject(buffer,bitmap);
    RECT row{0,0,320,32};FillRect(buffer,&row,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    DRAWITEMSTRUCT draw{ODT_COMBOBOX,IDC_NOTES_SCOPE,1,ODA_DRAWENTIRE,0,scope,buffer,row,0};
    SendMessageW(manager,WM_DRAWITEM,IDC_NOTES_SCOPE,reinterpret_cast<LPARAM>(&draw));
    bool visibleText=false;for(int y=3;y<29 && !visibleText;++y)for(int x=12;x<280;++x)if(GetPixel(buffer,x,y)!=g_themeColors.crControl){visibleText=true;break;}
    Check(GetPixel(buffer,2,2)==g_themeColors.crControl && visibleText,"All clients dropdown paints readable options on the Notes theme instead of a blank white box");
    SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);ReleaseDC(scope,screen);
  }
  SendDlgItemMessageW(manager,IDC_NOTES_SCOPE,CB_SETCURSEL,1,0);SendMessageW(manager,WM_COMMAND,MAKEWPARAM(IDC_NOTES_SCOPE,CBN_SELCHANGE),0);
  Check(lib.allClients && lib.readOnly && lib.state!=&state && lib.origins.size()==lib.state->pages.size(),"All clients uses a separate read-only preview snapshot with explicit ownership");
  SetDlgItemTextW(manager,IDC_NOTES_QUERY,L"Unique B content");NotesLibraryFill(manager,lib);
  Check(lib.results.size()==1 && lib.origins[lib.results.front()].first==L"Inspector B" && NotesWindowText(GetDlgItem(manager,IDC_NOTES_INFO)).find(L"Client: Inspector B")!=std::wstring::npos,"Combined search resolves duplicate IDs to the right client and shows ownership");
  for (auto action:{IDC_NOTES_ARCHIVE,IDC_NOTES_DELETE,IDC_NOTES_RESTORE,IDC_NOTES_EMPTY_TRASH}) Command(manager,action);
  Check(client_notes::ReadNotebook(aRoot).notebook_snapshot.bytes==aBytes && client_notes::ReadNotebook(bRoot).notebook_snapshot.bytes==bBytes,"Cross-client preview cannot mutate either notebook even through direct command messages");
  SetDlgItemTextW(manager,IDC_NOTES_QUERY,L"");SendDlgItemMessageW(manager,IDC_NOTES_SCOPE,CB_SETCURSEL,0,0);SendMessageW(manager,WM_COMMAND,MAKEWPARAM(IDC_NOTES_SCOPE,CBN_SELCHANGE),0);
  Check(!lib.readOnly && lib.state==&state && lib.selected.empty(),"Returning to This client restores its notebook and clears selections");
  const auto dpi=GetDpiForWindow(manager);SetWindowPos(manager,nullptr,0,0,ScaleByDpi(1080,dpi),ScaleByDpi(720,dpi),SWP_NOMOVE|SWP_NOZORDER);DestroyWindow(manager);
  Check(GetPrivateProfileIntW(L"notes_library",L"width",0,g_sConfigPath.c_str())==1080 && GetPrivateProfileIntW(L"notes_library",L"height",0,g_sConfigPath.c_str())==720,"All notes remembers its dimensions in DPI-independent units");
  Type(owner,L" - pending before switch");OpenRequestedNotesClient(reinterpret_cast<LPARAM>(new NotesClientRequest{L"Inspector B",0}));
  owner=g_hNotesWindow;Check(owner && State(owner).clientName==L"Inspector B" && NotesPlain(client_notes::ReadNotebook(aRoot).pages.back()).find(L"pending before switch")!=std::wstring::npos,"Switching clients flushes pending edits to the source before opening the destination");
  CloseClientNotesDialog(owner,State(owner));owner=Open(L"Inspector A");Type(owner,L" save must fail");
  auto newer=State(owner).original.pages;newer.front().ticket=L"Other writer";Check(client_notes::WriteNotebook(aRoot,newer,State(owner).original),"Concurrent source edit creates a switch conflict");
  OpenRequestedNotesClient(reinterpret_cast<LPARAM>(new NotesClientRequest{L"Inspector B",0}));
  Check(g_hNotesWindow==owner && State(owner).clientName==L"Inspector A" && State(owner).saveFailed,"A failed source save refuses the client switch and keeps its draft");DestroyWindow(owner);
  // Exercise production client deletion, then re-read B byte for byte.
  const auto retained=client_notes::ReadNotebook(bRoot).notebook_snapshot.bytes;
  std::wstring deleteOutcome;
  Check(DeleteEntireClient(L"Inspector A",true,std::nullopt,deleteOutcome),"Production client deletion succeeds for the owned disposable client");
  Check(client_notes::ReadNotebook(bRoot).notebook_snapshot.bytes==retained,"Removing one client container leaves the other client's notebook unchanged");
  OpenRequestedNotesClient(reinterpret_cast<LPARAM>(new NotesClientRequest{L"Inspector A",0}));
  Check(!g_hNotesWindow,"A stale picker selection cannot reopen a deleted client");
}
static void NotesResizeWrapping() {
  Fixture(L"Wrap QA");HWND owner=Open(L"Wrap QA");Command(owner,IDC_NOTES_ADD_TAB);
  Type(owner,L"Two new office printers are ready for installation. Existing queues on PRINT01 remain in service while the vendor confirms the rollout window. Two new office printers are ready for installation. Existing queues on PRINT01 remain in service while the vendor confirms the rollout window. Two new office printers are ready for installation. Existing queues on PRINT01 remain in service while the vendor confirms the rollout window.");Save(owner);
  const auto dpi=GetDpiForWindow(owner);
  ShowWindow(owner,SW_SHOWNOACTIVATE);
  SetWindowPos(owner,nullptr,0,0,ScaleByDpi(1500,dpi),ScaleByDpi(700,dpi),SWP_NOMOVE|SWP_NOZORDER);
  RedrawWindow(owner,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
  const auto wide=SendMessageW(Edit(owner),EM_GETLINECOUNT,0,0);
  RECT wideArea{},wideFormat{};GetClientRect(Edit(owner),&wideArea);SendMessageW(Edit(owner),EM_GETRECT,0,reinterpret_cast<LPARAM>(&wideFormat));
  SetWindowPos(owner,nullptr,0,0,ScaleByDpi(900,dpi),ScaleByDpi(700,dpi),SWP_NOMOVE|SWP_NOZORDER);
  RedrawWindow(owner,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
  const auto narrow=SendMessageW(Edit(owner),EM_GETLINECOUNT,0,0);
  RECT format{},area{};SendMessageW(Edit(owner),EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));GetClientRect(Edit(owner),&area);
  std::cout<<"Wrapping: "<<wide<<" wide, "<<narrow<<" narrow; rectangle "<<format.right<<" / "<<area.right<<"; wide rectangle "<<wideFormat.right<<" / "<<wideArea.right<<"; options "<<SendMessageW(Edit(owner),EM_GETOPTIONS,0,0)<<std::endl;
  Check(narrow>wide && format.right<area.right,"Narrowing Notes reflows long paragraphs inside its padded editor");
  Command(owner,IDC_NOTES_ADD_TAB);
  NotesCharacterFormat(owner,State(owner),CFM_SIZE,0,0,220);
  Type(owner,L"Two new office printers are ready for installation. Existing queues on PRINT01 remain in service while the vendor confirms the rollout window.");
  const auto prose=NotesWindowText(Edit(owner));
  SetWindowPos(owner,nullptr,0,0,ScaleByDpi(1120,dpi),ScaleByDpi(960,dpi),SWP_NOMOVE|SWP_NOZORDER);
  RedrawWindow(owner,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
  // Calibrate the fixture to fit this host's wide editor but cross its narrow
  // edge. Font metrics and DPI virtualization vary between native test hosts.
  RECT wideProseRect{};SendMessageW(Edit(owner),EM_GETRECT,0,reinterpret_cast<LPARAM>(&wideProseRect));
  bool calibrated=false;
  for(LONG height=200;height<=400;height+=5) {
    Select(Edit(owner),0,-1);NotesCharacterFormat(owner,State(owner),CFM_SIZE,0,0,height);
    RedrawWindow(owner,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    POINT end{};SendMessageW(Edit(owner),EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&end),prose.size()-1);
    if(SendMessageW(Edit(owner),EM_GETLINECOUNT,0,0)==1 && end.x>wideProseRect.right-ScaleByDpi(100,dpi)+10 && end.x<wideProseRect.right-12) {calibrated=true;break;}
  }
  const auto beforeSmallDrag=SendMessageW(Edit(owner),EM_GETLINECOUNT,0,0);
  for(int width=1110;width>=1020;width-=10) {
    SetWindowPos(owner,nullptr,0,0,ScaleByDpi(width,dpi),ScaleByDpi(width-160,dpi),SWP_NOMOVE|SWP_NOZORDER);
    RedrawWindow(owner,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
  }
  std::cout<<"Small drag wrapping: "<<beforeSmallDrag<<" -> "<<SendMessageW(Edit(owner),EM_GETLINECOUNT,0,0)<<"; DPI "<<dpi<<"; text preserved "<<(NotesWindowText(Edit(owner))==prose)<<std::endl;
  Check(calibrated && SendMessageW(Edit(owner),EM_GETLINECOUNT,0,0)>beforeSmallDrag && NotesWindowText(Edit(owner))==prose,"Incremental small drag reflows a single short paragraph without changing its text");
  CloseClientNotesDialog(owner,State(owner));
}
struct NoteTransitionCounts {
  unsigned visibleStreams=0,prematureEditors=0,newEditors=0;
};
static LRESULT CALLBACK ObserveNoteTransition(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
  auto &counts=*reinterpret_cast<NoteTransitionCounts *>(data);
  if (message==EM_STREAMIN && (GetWindowLongPtrW(window,GWL_STYLE)&WS_VISIBLE)) ++counts.visibleStreams;
  if (message==WM_WINDOWPOSCHANGED && (GetWindowLongPtrW(window,GWL_STYLE)&WS_VISIBLE) && GetDlgCtrlID(window)==IDC_CLIENT_NOTES_TEXT) {
    RECT area{};GetClientRect(window,&area);
    if (area.right<ScaleByDpi(500,GetDpiForWindow(window)) || area.bottom<ScaleByDpi(150,GetDpiForWindow(window))) ++counts.prematureEditors;
  }
  if (message==WM_PARENTNOTIFY && LOWORD(w)==WM_CREATE) {
    HWND child=reinterpret_cast<HWND>(l);wchar_t name[32]{};GetClassNameW(child,name,32);
    if (!wcscmp(name,L"RICHEDIT50W")) {
      ++counts.newEditors;
      if (GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE) ++counts.prematureEditors;
      SetWindowSubclass(child,ObserveNoteTransition,id,data);
    }
  }
  if (message==WM_NCDESTROY) RemoveWindowSubclass(window,ObserveNoteTransition,id);
  return DefSubclassProc(window,message,w,l);
}
static void AddDeleteTransitions(HWND owner) {
  auto &workspace=State(owner);
  for (int i=0;i<3;++i) {Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Neighbor selection fixture");Save(owner);}
  const auto left=workspace.pages[workspace.visiblePages[0]].id,middle=workspace.pages[workspace.visiblePages[1]].id;
  Command(owner,IDC_NOTES_DELETE);Check(workspace.pages[workspace.activePage].id==middle,"Deleting the rightmost tab selects its neighbor instead of jumping to the first tab");
  Command(owner,IDC_NOTES_CLOSE_TAB);Check(workspace.pages[workspace.activePage].id==left,"Closing the active middle tab selects its left neighbor");
  Command(owner,IDC_NOTES_CLOSE_ALL);
  Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Keep this document and its undo history");Save(owner);
  const HWND kept=Edit(owner);const auto text=NotesWindowText(kept);
  NoteTransitionCounts counts;
  SetWindowSubclass(owner,ObserveNoteTransition,27,reinterpret_cast<DWORD_PTR>(&counts));
  SetWindowSubclass(kept,ObserveNoteTransition,27,reinterpret_cast<DWORD_PTR>(&counts));
  SetWindowSubclass(GetDlgItem(owner,IDC_NOTES_TABS),ObserveNoteTransition,27,reinterpret_cast<DWORD_PTR>(&counts));
  std::vector<double> times,removeTimes;
  for (int i=0;i<12;++i) {
    auto start=std::chrono::steady_clock::now();Command(owner,IDC_NOTES_ADD_TAB);
    times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
    const HWND transient=Edit(owner);
    Check(transient!=kept && (GetWindowLongPtrW(transient,GWL_STYLE)&WS_VISIBLE),"Added note exposes only its fully sized editor");
    if (i%2) {Type(owner,L"Disposable deletion test");Save(owner);}
    start=std::chrono::steady_clock::now();Command(owner,i%2 ? IDC_NOTES_DELETE : IDC_NOTES_CLOSE_TAB);
    removeTimes.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
    Check(Edit(owner)==kept && NotesWindowText(kept)==text,"Delete or close switches back to the cached document without restreaming it");
    if (!(i%2)) Check(!IsWindow(transient),"Pristine editor retires after the replacement is ready");
  }
  Check(counts.newEditors==12 && counts.visibleStreams==0 && counts.prematureEditors==0,"Repeated add/delete never streams visible content or shows a temporary 100x100 editor");
  std::sort(times.begin(),times.end());std::cout<<"Add-note timings (ms): median="<<times[times.size()/2]<<", max="<<times.back()<<std::endl;
  std::sort(removeTimes.begin(),removeTimes.end());std::cout<<"Delete/close-note timings (ms): median="<<removeTimes[removeTimes.size()/2]<<", max="<<removeTimes.back()<<std::endl;
  Command(owner,IDC_NOTES_CLOSE_TAB);
  Check(!HasActiveNote(workspace) && !workspace.attachedEditor && (GetWindowLongPtrW(GetDlgItem(owner,IDC_NOTES_EMPTY),GWL_STYLE)&WS_VISIBLE),"Closing the last tab reveals a clean empty state");
  RemoveWindowSubclass(owner,ObserveNoteTransition,27);
  RemoveWindowSubclass(GetDlgItem(owner,IDC_NOTES_TABS),ObserveNoteTransition,27);
  for (const auto &[id,edit] : workspace.editors) RemoveWindowSubclass(edit,ObserveNoteTransition,27);
  Save(owner);
}
static void DirectTemplateEditing(HWND owner) {
  auto &workspace=State(owner);NotesTemplates library{owner,&workspace};Check(NotesLoadTemplates(library),"Direct template library loads with no client note open");
  const auto count=workspace.pages.size();
  HWND dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_TEMPLATE_EDITOR),owner,NotesTemplateEditorProc,reinterpret_cast<LPARAM>(&library));
  Check(!(SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_GETOPTIONS,0,0)&ECO_READONLY) && !IsWindowEnabled(GetDlgItem(dialog,IDC_NOTES_SAVE_TEMPLATE)),"Templates can be authored directly without an open client note");
  Command(dialog,IDC_NOTES_ARCHIVE);
  Check(!library.draft.id && library.dirty && workspace.pages.size()==count,"New template starts a draft without creating a client note");
  CHARFORMAT2W blankFormat{sizeof(blankFormat)};SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&blankFormat));
  Check(blankFormat.yHeight==240 && !(blankFormat.dwEffects&CFE_BOLD),"Blank templates start in 12-point normal text rather than inheriting a prior heading");
  PARAFORMAT2 blankParagraph{sizeof(blankParagraph)};SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&blankParagraph));
  Check(blankParagraph.dySpaceBefore==0 && blankParagraph.dySpaceAfter==0,"Blank template drafts have no added paragraph gaps");
  blankParagraph.dwMask=PFM_SPACEBEFORE|PFM_SPACEAFTER|PFM_LINESPACING;blankParagraph.dySpaceBefore=120;blankParagraph.dySpaceAfter=80;blankParagraph.bLineSpacingRule=2;
  SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_SETPARAFORMAT,0,reinterpret_cast<LPARAM>(&blankParagraph));
  SendDlgItemMessageW(dialog,IDC_NOTES_STYLE,CB_SETCURSEL,0,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_NOTES_STYLE,CBN_SELENDOK),0);
  SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&blankParagraph));
  Check(blankParagraph.dySpaceBefore==0 && blankParagraph.dySpaceAfter==0 && blankParagraph.bLineSpacingRule==5 && blankParagraph.dyLineSpacing==20,"Reselecting Normal text resets template paragraph and line spacing");
  Check(OpenClipboard(dialog)!=FALSE,"Template clipboard fixture opens");EmptyClipboard();
  const wchar_t paste[]=L"Pasted Unicode template \x4e2d\x6587";HGLOBAL clipboard=GlobalAlloc(GMEM_MOVEABLE,sizeof(paste));
  memcpy(GlobalLock(clipboard),paste,sizeof(paste));GlobalUnlock(clipboard);SetClipboardData(CF_UNICODETEXT,clipboard);CloseClipboard();
  SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,WM_PASTE,0,0);
  Check(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_PREVIEW))==paste && library.dirty,"Template editor accepts bounded plain-text paste despite embedded-object protection");
  BYTE priorKeys[256]{},keys[256]{};GetKeyboardState(priorKeys);memcpy(keys,priorKeys,256);keys[VK_CONTROL]=0x80;
  SetWindowTextW(GetDlgItem(dialog,IDC_NOTES_PREVIEW),L"");SetKeyboardState(keys);SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,WM_KEYDOWN,'V',0);SetKeyboardState(priorKeys);
  Check(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_PREVIEW))==paste,"Ctrl+V uses the template's bounded paste route rather than RichEdit's blocked internal route");
  SetWindowTextW(GetDlgItem(dialog,IDC_NOTES_PREVIEW),L"");memcpy(keys,priorKeys,256);keys[VK_SHIFT]=0x80;
  SetKeyboardState(keys);SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,WM_KEYDOWN,VK_INSERT,0);SetKeyboardState(priorKeys);
  Check(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_PREVIEW))==paste,"Shift+Insert pastes reusable template text");
  SetDlgItemTextW(dialog,IDC_NOTES_TEMPLATE_NAME,L"Direct template QA");SetDlgItemTextW(dialog,IDC_NOTES_TEMPLATE_HINT,L"Use during a maintenance handover");
  HWND body=GetDlgItem(dialog,IDC_NOTES_PREVIEW);SetWindowTextW(body,L"Template heading\r\nOwner:\r\nNext action:");
  Select(body,0,16);SendDlgItemMessageW(dialog,IDC_NOTES_STYLE,CB_SETCURSEL,1,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_NOTES_STYLE,CBN_SELCHANGE),0);
  CHARFORMAT2W format{sizeof(format)};SendMessageW(body,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
  Check(format.yHeight==480 && (format.dwEffects&CFE_BOLD),"Direct editor applies real heading formatting");
  Check(NotesTemplateEditorSave(dialog,library) && library.draft.id && !library.dirty,"Direct formatted template persists independently");
  const auto id=library.draft.id;const auto committed=library.draft.rtf;const auto plain=NotesWindowText(body);
  Select(body,-1,-1);SendMessageW(body,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"\r\nUnsaved draft"));
  g_bThemeIsDark=false;BuildThemeColors(false,g_themeColors);UpdateThemeBrushes();RefreshNotesLibraryForTheme(dialog);
  Check(library.dirty && NotesWindowText(body).find(L"Unsaved draft")!=std::wstring::npos,"Theme change keeps unsaved template content");
  g_bThemeIsDark=true;BuildThemeColors(true,g_themeColors);UpdateThemeBrushes();RefreshNotesLibraryForTheme(dialog);
  answers.push_back(IDCANCEL);SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_SETCURSEL,0,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_NOTES_RESULTS,LBN_SELCHANGE),0);
  Check(library.draft.id==id && library.dirty && SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_GETCURSEL,0,0)==library.selected,"Cancel switching templates keeps the draft and list selection");
  answers.push_back(IDNO);Command(dialog,IDC_NOTES_ARCHIVE);
  Check(!library.draft.id && library.draft.rtf.find("Unsaved draft")==std::string::npos,"Discard opens a fresh template without saving unwanted changes");
  SetDlgItemTextW(dialog,IDC_NOTES_TEMPLATE_NAME,library.pages.front().name.c_str());
  Check(!NotesTemplateEditorSave(dialog,library) && library.dirty && !library.draft.id,"Duplicate template names retain the editable draft");
  SetDlgItemTextW(dialog,IDC_NOTES_TEMPLATE_NAME,L"Use immediately QA");SetWindowTextW(body,L"Save and use this template in one place");
  answers.push_back(IDYES);Check(NotesTemplateEditorLeave(dialog,library) && !library.dirty,"Save on navigation commits the template draft");
  const auto pageCount=workspace.pages.size();SetWindowTextW(body,L"Latest edit before Use");Command(dialog,IDC_NOTES_TEMPLATE_USE);
  Check(workspace.pages.size()==pageCount+1 && NotesWindowText(Edit(owner))==L"Latest edit before Use" && workspace.pages.back().ticket.empty(),"Use template saves the latest editor changes and creates an independent client note");
  Select(Edit(owner),-1,-1);memcpy(keys,priorKeys,256);keys[VK_CONTROL]=0x80;SetKeyboardState(keys);
  SendMessageW(Edit(owner),WM_KEYDOWN,'V',0);SetKeyboardState(priorKeys);
  Check(NotesWindowText(Edit(owner)).find(paste)!=std::wstring::npos,"Client notes Ctrl+V also uses the bounded plain-text paste route");
  DestroyWindow(dialog);
  NotesTemplates stale{owner,&workspace};Check(NotesLoadTemplates(stale),"Conflict test loads a saved template snapshot");
  dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_TEMPLATE_EDITOR),owner,NotesTemplateEditorProc,reinterpret_cast<LPARAM>(&stale));
  SetWindowTextW(GetDlgItem(dialog,IDC_NOTES_PREVIEW),L"Keep this conflicting draft");
  auto newer=stale;auto pages=newer.pages;pages.front().ticket=L"Changed by another window";Check(NotesSaveTemplates(owner,newer,pages),"Concurrent template edit commits safely");
  Check(!NotesTemplateEditorSave(dialog,stale) && stale.dirty && NotesWindowText(GetDlgItem(dialog,IDC_NOTES_PREVIEW))==L"Keep this conflicting draft","Failed concurrent save preserves the entire template draft");
  DestroyWindow(dialog);
  NotesTemplates reopened{owner,&workspace};Check(NotesLoadTemplates(reopened),"Direct template library reopens");
  const auto found=std::find_if(reopened.pages.begin(),reopened.pages.end(),[&](const auto &page){return page.id==id;});
  Check(found!=reopened.pages.end() && found->rtf==committed && NotesPlain(*found)==plain,"Formatted template survives reopening without discarded edits");
  dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_TEMPLATE_EDITOR),owner,NotesTemplateEditorProc,reinterpret_cast<LPARAM>(&reopened));
  for (int width : {900,1000,1500}) {
    SetWindowPos(dialog,nullptr,0,0,ScaleByDpi(width,GetDpiForWindow(dialog)),ScaleByDpi(600,GetDpiForWindow(dialog)),SWP_NOMOVE|SWP_NOZORDER);
    RECT prior{};bool first=true;for (int button : {IDC_NOTES_ARCHIVE,IDC_NOTES_SAVE_TEMPLATE,IDC_NOTES_DELETE,IDC_NOTES_RESTORE,IDC_NOTES_TEMPLATE_USE,IDCANCEL}) {
      RECT bounds{};GetWindowRect(GetDlgItem(dialog,button),&bounds);Check(first || bounds.left>=prior.right,"Template actions stay separated across window sizes");prior=bounds;first=false;
    }
  }
  Check(SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_GETITEMHEIGHT,0,0)>=ScaleByDpi(24,GetDpiForWindow(dialog)),"Template list uses readable Notes-sized rows");
  Check(SendDlgItemMessageW(dialog,IDC_NOTES_TEMPLATE_NAME,WM_GETFONT,0,0)==reinterpret_cast<LRESULT>(workspace.uiFont),"Template fields share the workspace's readable font");
  DestroyWindow(dialog);Command(owner,IDC_NOTES_CLOSE_ALL);Save(owner);
}
static void DirectTrashAndCompactHelp(HWND owner) {
  auto &state=State(owner);
  Command(owner,IDC_NOTES_TRASH);Check(openedLibraryFilter==3,"Direct Trash opens the deleted view immediately");
  NotesLibrary trash{owner,&state};trash.initialFilter=3;
  HWND library=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&trash));
  Check(SendDlgItemMessageW(library,IDC_NOTES_FILTER,CB_GETCURSEL,0,0)==3 && NotesWindowText(library).find(L"Trash - ")==0,"Trash window starts with a named Trash filter and caption");
  Check(std::all_of(trash.results.begin(),trash.results.end(),[&](size_t i){return state.pages[i].deleted;}),"Direct Trash excludes kept notes");
  Check((GetWindowLongPtrW(GetDlgItem(library,IDC_NOTES_EMPTY_TRASH),GWL_STYLE)&WS_VISIBLE)!=0,"Direct Trash exposes Empty trash");DestroyWindow(library);
  HWND help=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_HELP),owner,NotesHelpProc,0);
  Check(GetDlgItem(help,IDC_NOTES_TRASH)!=nullptr,"Compact Help offers a direct Trash entry");
  Check(NotesWindowText(GetDlgItem(help,IDC_NOTES_HELP_TICKETS)).find(L"More > Link browser")!=std::wstring::npos,"Quick Help explains where to choose the regular PC browser for links");
  Check(NotesWindowText(GetDlgItem(help,IDC_NOTES_HELP_TEMPLATES)).find(L"directly")!=std::wstring::npos,"Quick Help describes direct template editing");DestroyWindow(help);
  Check(std::any_of(std::begin(kNotesActions),std::end(kNotesActions),[](const auto &action){return action.id==IDC_NOTES_TRASH && !action.needsNote;}),"Searchable Trash action works without an open note");
}
static void NotesLinksAndContextMenu(HWND owner) {
  Command(owner,IDC_NOTES_ADD_TAB);auto &state=State(owner);
  const auto count=shellOpens;
  answers.push_back(IDNO);OpenNotesUrl(owner,state,L"https://example.com/ordinary");
  Check(shellOpens==count && lastNotice.find(L"default browser")!=std::wstring::npos,"Ordinary note link explains the PC browser and cancel does not launch");
  answers.push_back(IDYES);OpenNotesUrl(owner,state,L"https://example.com/ordinary");
  Check(shellOpens==count+1 && shellTarget==L"https://example.com/ordinary","Ordinary note link uses Windows association without client-profile arguments");
  OpenNotesUrl(owner,state,L"file:///unsafe");Check(shellOpens==count+1,"Ordinary note links reject non-web URLs");
  Check(OpenClipboard(owner)!=FALSE,"Context menu clipboard fixture opens");EmptyClipboard();
  const wchar_t text[]=L"Right-click pasted \x4e2d\x6587";HGLOBAL clip=GlobalAlloc(GMEM_MOVEABLE,sizeof(text));
  memcpy(GlobalLock(clip),text,sizeof(text));GlobalUnlock(clip);SetClipboardData(CF_UNICODETEXT,clip);CloseClipboard();
  const auto menus=popupCalls;popupAction=5;SendMessageW(Edit(owner),WM_RBUTTONUP,0,MAKELPARAM(20,20));
  if (!(popupCalls==menus+1 && popupPasteEnabled && !popupCutEnabled && NotesWindowText(Edit(owner))==text)) std::cout << "Context fixture: calls " << popupCalls-menus << " paste " << popupPasteEnabled << " cut " << popupCutEnabled << " text length " << NotesWindowText(Edit(owner)).size() << std::endl;
  Check(popupCalls==menus+1 && popupPasteEnabled && !popupCutEnabled && NotesWindowText(Edit(owner))==text,"Actual right-button message exposes Paste and inserts Unicode text");
  Select(Edit(owner),0,5);SendMessageW(Edit(owner),WM_CONTEXTMENU,reinterpret_cast<WPARAM>(Edit(owner)),MAKELPARAM(-1,-1));
  CHARRANGE selection{};NotesSelection(Edit(owner),selection);
  Check(popupCutEnabled && selection.cpMin==0 && selection.cpMax==5,"Keyboard context menu keeps the selection and enables editing actions");
  Save(owner);Command(owner,IDC_NOTES_CLOSE_ALL);Save(owner);
}
static void EmptyDraftRestart() {
  Fixture(L"Empty draft QA");HWND owner=Open(L"Empty draft QA");
  Command(owner,IDC_NOTES_ADD_TAB);auto &state=State(owner);state.pages[state.activePage].name=L"Planned checklist";state.metadataChanged=true;
  Command(owner,IDC_NOTES_ADD_TAB);state.pages[state.activePage].status=2;state.metadataChanged=true;
  Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Written content");state.pages[state.activePage].name=L"Written work";state.metadataChanged=true;
  Command(owner,IDC_NOTES_ADD_TAB);Command(owner,IDC_NOTES_ADD_TAB);
  CloseClientNotesDialog(owner,state);Check(!IsWindow(owner),"Closing Notes completes after pruning unused blank drafts");
  owner=Open(L"Empty draft QA");auto &restored=State(owner);
  Check(std::none_of(restored.pages.begin(),restored.pages.end(),NotesIsUnusedBlankDraft),"Unused blank Untitled drafts do not accumulate across restart");
  Check(std::any_of(restored.pages.begin(),restored.pages.end(),[](const auto &p){return p.name==L"Planned checklist";}),"Named blank notes survive draft cleanup");
  Check(std::any_of(restored.pages.begin(),restored.pages.end(),[](const auto &p){return p.status==2;}),"Blank notes with meaningful metadata survive cleanup");
  Check(std::any_of(restored.pages.begin(),restored.pages.end(),[](const auto &p){return p.name==L"Written work" && NotesPlain(p)==L"Written content";}),"Written notes survive draft cleanup");
  Command(owner,IDC_NOTES_ADD_TAB);Check(State(owner).pages[State(owner).activePage].name==L"Untitled 2","New blank draft reuses the lowest available name while preserving the metadata note named Untitled");
  CloseClientNotesDialog(owner,State(owner));
}
static void CompactNormalSpacing() {
  Fixture(L"Spacing QA");HWND owner=Open(L"Spacing QA");Command(owner,IDC_NOTES_ADD_TAB);
  HWND edit=Edit(owner);CHARFORMAT2W font{sizeof(font)};SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&font));
  PARAFORMAT2 paragraph{sizeof(paragraph)};SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
  Check(font.yHeight==240,"New blank notes use 12-point normal text");
  Check(paragraph.dySpaceBefore==0 && paragraph.dySpaceAfter==0 && paragraph.bLineSpacingRule==5 && paragraph.dyLineSpacing==20,"New notes have single spacing and no added paragraph gaps");
  Type(owner,L"First line\r\n\r\nSecond line\r\n\r\nThird line");const auto text=NotesWindowText(edit);
  Select(edit,0,-1);paragraph.dwMask=PFM_SPACEBEFORE|PFM_SPACEAFTER|PFM_LINESPACING;paragraph.dySpaceBefore=120;paragraph.dySpaceAfter=80;paragraph.bLineSpacingRule=2;
  SendMessageW(edit,EM_SETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
  const auto lineY=[&](LONG line) {POINTL point{};const auto first=SendMessageW(edit,EM_LINEINDEX,line,0);SendMessageW(edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&point),first);return point.y;};
  const auto spacious=lineY(2)-lineY(0);
  SendDlgItemMessageW(owner,IDC_NOTES_STYLE,CB_SETCURSEL,0,0);SendMessageW(owner,WM_COMMAND,MAKEWPARAM(IDC_NOTES_STYLE,CBN_SELENDOK),0);
  SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
  Check(paragraph.dySpaceBefore==0 && paragraph.dySpaceAfter==0 && paragraph.bLineSpacingRule==5 && paragraph.dyLineSpacing==20,"Choosing the already-selected Normal text removes saved paragraph and double-line spacing");
  Check(lineY(2)-lineY(0)<spacious && lineY(1)-lineY(0)==lineY(2)-lineY(1),"Actual editor positions tighten while keeping blank lines one line high");
  Check(NotesWindowText(edit)==text,"Tightening spacing preserves every character and blank line");
  Select(edit,0,10);SendDlgItemMessageW(owner,IDC_NOTES_STYLE,CB_SETCURSEL,1,0);SendMessageW(owner,WM_COMMAND,MAKEWPARAM(IDC_NOTES_STYLE,CBN_SELCHANGE),0);
  Select(edit,0,0);SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&font));
  Check(font.yHeight==560 && paragraph.dySpaceAfter==200,"Heading size and paragraph spacing remain unchanged");
  Select(edit,0,10);SendDlgItemMessageW(owner,IDC_NOTES_STYLE,CB_SETCURSEL,2,0);SendMessageW(owner,WM_COMMAND,MAKEWPARAM(IDC_NOTES_STYLE,CBN_SELCHANGE),0);
  Select(edit,0,0);SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
  Check(paragraph.dySpaceBefore==240 && paragraph.dySpaceAfter==160,"Subheading paragraph spacing remains unchanged");
  Select(edit,0,-1);Command(owner,IDC_NOTES_CLEAR);SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&font));
  Check(font.yHeight==240 && paragraph.dySpaceBefore==0 && paragraph.dySpaceAfter==0,"Clear formatting restores compact 12-point normal text");
  Save(owner);CloseClientNotesDialog(owner,State(owner));owner=Open(L"Spacing QA");edit=Edit(owner);Select(edit,0,-1);
  SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
  Check(NotesWindowText(edit)==text && paragraph.dySpaceBefore==0 && paragraph.dySpaceAfter==0,"Compact spacing and original blank lines survive notebook close and reopen");
  client_notes::NotePage plain{L"Captured text",true,NotesPlainRtf(L"Capture\n\nNext line"),{}};NotesAddPage(owner,State(owner),std::move(plain));edit=Edit(owner);Select(edit,0,-1);
  SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&font));SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
  Check(font.yHeight==240 && paragraph.dySpaceBefore==0 && paragraph.dySpaceAfter==0,"Generated plain-text notes use compact spacing and 12-point text");
  CloseClientNotesDialog(owner,State(owner));
}
static void OldNotesSpacingUpgrade() {
  const auto seed=[&](const std::wstring &name) {
    Fixture(name);const auto root=g_sDataDir/L"Sites"/name;
    client_notes::NotePage page{L"Existing note",true,
      "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}\\f0\\fs28\\pard\\sb0\\sa80\\sl240\\slmult1 Body one\\par\\par Body two\\par "
      "\\pard\\sb0\\sa200\\fs56\\b Heading\\b0\\par "
      "\\pard\\sb240\\sa160\\fs32 Subheading\\par "
      "\\pard\\sb0\\sa80\\sl480\\slmult1\\fs28 Double spaced\\par "
      "\\pard\\sb120\\sa80\\sl240\\slmult1 Before gap\\par "
      "\\pard\\sb0\\sa100 Custom after}",{}};
    page.id=1;page.open=page.active=true;page.cursor=5;page.zoom=125;
    const auto original=client_notes::ReadNotebook(root);
    Check(client_notes::WriteNotebook(root,{page},original),"Old-spacing notebook fixture saves");return page;
  };
  const auto old=seed(L"Old spacing QA");HWND owner=Open(L"Old spacing QA");HWND edit=Edit(owner);
  const auto paraAt=[&](const wchar_t *word) {
    const auto text=NotesWindowText(edit);const auto offset=text.find(word);
    // GetWindowText uses CRLF, while RichEdit character ranges use one CR.
    const LONG position=static_cast<LONG>(offset-std::count(text.begin(),text.begin()+offset,L'\n'));
    Select(edit,position,position);PARAFORMAT2 p{sizeof(p)};SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&p));return p;
  };
  CHARRANGE cursor{};NotesSelection(edit,cursor);
  int numerator=0,denominator=0;SendMessageW(edit,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
  Check(cursor.cpMin==5 && numerator*100==125*denominator,"Old-note migration restores the remembered caret and zoom");
  Check(paraAt(L"Body one").dySpaceAfter==0 && paraAt(L"Body two").dySpaceAfter==0,"Existing body paragraphs automatically lose the old four-point gap");
  Select(edit,0,0);CHARFORMAT2W font{sizeof(font)};SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&font));
  Check(font.yHeight==280,"Old-note spacing migration preserves the chosen font size");
  Check(paraAt(L"Heading").dySpaceAfter==200 && paraAt(L"Subheading").dySpaceBefore==240 && paraAt(L"Subheading").dySpaceAfter==160,"Old-note migration preserves heading and subheading spacing");
  Check(paraAt(L"Double spaced").dySpaceAfter==80 && paraAt(L"Before gap").dySpaceBefore==120 && paraAt(L"Custom after").dySpaceAfter==100,"Custom paragraph and line spacing is kept");
  const auto text=NotesWindowText(edit);Check(text.find(L"Body one\r\n\r\nBody two")!=std::wstring::npos,"Old-note migration keeps explicit blank lines");
  Check(State(owner).pages.front().history.size()==1 && State(owner).pages.front().history.front().rtf==old.rtf,"The original formatted note is retained in version history before migration");
  SendMessageW(owner,WM_TIMER,1,0);Check(!State(owner).saveFailed && !State(owner).metadataChanged,"Automatic spacing upgrade uses normal autosave");
  CloseClientNotesDialog(owner,State(owner));owner=Open(L"Old spacing QA");edit=Edit(owner);
  Check(NotesWindowText(edit)==text && State(owner).pages.front().history.size()==1 && !State(owner).metadataChanged,"Migrated spacing persists and reopening creates no duplicate revisions");
  CloseClientNotesDialog(owner,State(owner));
  seed(L"Spacing conflict QA");owner=Open(L"Spacing conflict QA");auto &state=State(owner);
  auto concurrent=state.original.pages;concurrent.front().ticket=L"Concurrent writer";
  Check(client_notes::WriteNotebook(state.clientRoot,concurrent,state.original),"Concurrent edit arrives before migration autosave");
  Check(!SaveClientNotesDraft(owner,state,false) && state.saveFailed && client_notes::ReadNotebook(state.clientRoot).pages.front().ticket==L"Concurrent writer","Spacing migration cannot overwrite a concurrent notebook change");
  Check(state.pages.front().history.size()==1 && fs::exists(state.clientRoot/state.draftName),"Failed spacing migration save keeps its draft and original revision");DestroyWindow(owner);
}
static void DirectSnippetEditing() {
  Fixture(L"Snippet editor QA");HWND owner=Open(L"Snippet editor QA");Command(owner,IDC_NOTES_CLOSE_ALL);
  auto &workspace=State(owner);NotesTemplates library{owner,&workspace};library.snippets=true;
  Check(NotesLoadTemplates(library),"Direct snippet library loads");const auto count=workspace.pages.size(),savedBlocks=library.pages.size();
  HWND dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_TEMPLATE_EDITOR),owner,NotesTemplateEditorProc,reinterpret_cast<LPARAM>(&library));
  Check(IsWindowEnabled(GetDlgItem(owner,IDC_NOTES_SNIPPETS)) && NotesWindowText(GetDlgItem(dialog,IDC_NOTES_ARCHIVE))==L"New snippet","Snippets stays available with a visible New snippet action when no note is open");
  Check(!IsWindowEnabled(GetDlgItem(dialog,IDC_NOTES_TEMPLATE_USE)) && !IsWindowEnabled(GetDlgItem(dialog,IDC_NOTES_SAVE_TEMPLATE)),"Snippet authoring does not require a note, while insertion and capture do");
  Command(dialog,IDC_NOTES_ARCHIVE);SetDlgItemTextW(dialog,IDC_NOTES_TEMPLATE_NAME,L"Direct QA block");SetDlgItemTextW(dialog,IDC_NOTES_TEMPLATE_HINT,L"Use before handover");
  HWND body=GetDlgItem(dialog,IDC_NOTES_PREVIEW);SetWindowTextW(body,L"Reusable heading\r\nNext step:");Select(body,0,16);Command(dialog,IDC_NOTES_BOLD);
  Check(NotesTemplateEditorSave(dialog,library) && workspace.pages.size()==count,"A formatted snippet can be written and saved directly without creating a note");const auto id=library.draft.id;const auto rtf=library.draft.rtf;
  Select(body,-1,-1);SendMessageW(body,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L" unwanted"));answers.push_back(IDCANCEL);Command(dialog,IDC_NOTES_ARCHIVE);
  Check(library.draft.id==id && library.dirty && NotesWindowText(body).find(L"unwanted")!=std::wstring::npos,"Cancelling snippet navigation keeps unsaved text");DestroyWindow(dialog);
  NotesTemplates reopened{owner,&workspace};reopened.snippets=true;Check(NotesLoadTemplates(reopened) && reopened.pages.back().rtf==rtf,"Saved snippet and formatting survive reopening independently of discarded edits");
  Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Before\r\nAfter");Save(owner);Select(Edit(owner),0,6);
  reopened.startFromNote=true;dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_TEMPLATE_EDITOR),owner,NotesTemplateEditorProc,reinterpret_cast<LPARAM>(&reopened));
  Check(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_PREVIEW))==L"Before" && !reopened.draft.id && reopened.dirty,"From selection opens an editable snippet draft containing only selected text");
  SetDlgItemTextW(dialog,IDC_NOTES_TEMPLATE_NAME,L"Captured QA block");SetWindowTextW(GetDlgItem(dialog,IDC_NOTES_PREVIEW),L"Edited captured block");
  const auto pages=workspace.pages.size();Command(dialog,IDC_NOTES_TEMPLATE_USE);
  Check(workspace.pages.size()==pages && NotesWindowText(Edit(owner))==L"Edited captured block\r\nAfter","Insert saves the latest snippet draft and replaces only the selected text without opening a tab");
  const auto inserted=NotesWindowText(Edit(owner));answers.push_back(IDOK);Command(dialog,IDC_NOTES_DELETE);
  Check(NotesWindowText(Edit(owner))==inserted && reopened.pages.size()==savedBlocks+1,"Deleting a saved snippet leaves already inserted text intact");DestroyWindow(dialog);
  NotesTemplates stale{owner,&workspace};stale.snippets=true;Check(NotesLoadTemplates(stale),"Snippet conflict fixture loads");
  dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_TEMPLATE_EDITOR),owner,NotesTemplateEditorProc,reinterpret_cast<LPARAM>(&stale));SetWindowTextW(GetDlgItem(dialog,IDC_NOTES_PREVIEW),L"Conflicting snippet draft");
  auto newer=stale;auto changed=newer.pages;changed.front().ticket=L"Other editor";Check(NotesSaveTemplates(owner,newer,changed),"Another snippet editor commits");
  Check(!NotesTemplateEditorSave(dialog,stale) && stale.dirty && NotesWindowText(GetDlgItem(dialog,IDC_NOTES_PREVIEW))==L"Conflicting snippet draft","Concurrent snippet save keeps the draft and protects newer library content");
  DestroyWindow(dialog);CloseClientNotesDialog(owner,workspace);
}
static std::vector<uint64_t> TabIds(const ClientNotesDialogState &state) {
  std::vector<uint64_t> ids;for(auto index:state.visiblePages) ids.push_back(state.pages[index].id);return ids;
}
static void NotesTabOrdering() {
  Fixture(L"Tab order QA");HWND owner=Open(L"Tab order QA");Command(owner,IDC_NOTES_CLOSE_ALL);
  auto &state=State(owner);
  for(const auto *name:{L"Alpha",L"Beta",L"Gamma"}) {
    client_notes::NotePage page{name,true,NotesPlainRtf(std::wstring(name)+L" content"),{}};
    Check(NotesAddPage(owner,state,std::move(page)),"Tab-order fixture saves");
  }
  const auto initial=TabIds(state);HWND tabs=GetDlgItem(owner,IDC_NOTES_TABS);
  ShowWindow(owner,SW_SHOWNOACTIVATE);
  RECT first{},last{};TabCtrl_GetItemRect(tabs,0,&first);TabCtrl_GetItemRect(tabs,2,&last);
  const auto down=MAKELPARAM(first.left+18,(first.top+first.bottom)/2);
  const auto move=MAKELPARAM(last.left+18,(last.top+last.bottom)/2);
  SendMessageW(tabs,WM_LBUTTONDOWN,MK_LBUTTON,down);
  HWND retained=Edit(owner);Select(retained,2,2);Type(owner,L" draft");
  state.pages[state.activePage].ticket=L"QA-173";state.pages[state.activePage].tags={L"handover"};state.metadataChanged=true;
  const auto text=NotesWindowText(retained);
  Check(GetCapture()==tabs && state.dragTab==0,"Visible native tab takes capture after selecting the drag source");
  SendMessageW(tabs,WM_MOUSEMOVE,MK_LBUTTON,move);
  Check(GetCapture()==tabs && state.dragging && state.dragTarget==2,"Native mouse movement retains capture and identifies its drop target");
  RECT area{};GetClientRect(tabs,&area);HDC screen=GetDC(tabs),dc=CreateCompatibleDC(screen);
  HBITMAP bitmap=CreateCompatibleBitmap(screen,area.right,area.bottom);auto old=SelectObject(dc,bitmap);
  SendMessageW(tabs,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
  Check(GetPixel(dc,last.right-1,(last.top+last.bottom)/2)==g_themeColors.crAccent,"Native drag paints a themed insertion marker");
  SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(tabs,screen);
  SendMessageW(tabs,WM_LBUTTONUP,0,move);
  Check(TabIds(state)==std::vector<uint64_t>({initial[1],initial[2],initial[0]}) && state.dragTab==-1 && GetCapture()!=tabs,"Dropping moves the source to the destination and releases capture");
  Check(Edit(owner)==retained && state.pages[state.activePage].id==initial[0] && NotesWindowText(retained)==text && SendMessageW(retained,EM_CANUNDO,0,0),"Reorder preserves active note identity, cached editor, content and undo");
  Check(state.pages[state.activePage].ticket==L"QA-173" && state.pages[state.activePage].tags==std::vector<std::wstring>({L"handover"}),"Reorder keeps ticket and tags attached to their original note");
  bool aligned=true;for(size_t i=0;i<state.pages.size();++i) aligned=aligned && state.baselines[i]==state.pages[i].rtf;
  Check(aligned,"Reorder keeps formatting baselines aligned with their pages");
  const auto reordered=TabIds(state);TabCtrl_GetItemRect(tabs,0,&first);TabCtrl_GetItemRect(tabs,2,&last);
  SendMessageW(tabs,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(first.left+18,15));
  SendMessageW(tabs,WM_LBUTTONUP,0,MAKELPARAM(first.left+18,15));
  Check(TabIds(state)==reordered && state.dragTab==-1 && GetCapture()!=tabs,"A normal tab click does not reorder or retain capture");
  SendMessageW(tabs,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(first.left+18,15));
  SendMessageW(tabs,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(last.left+18,15));SendMessageW(tabs,WM_CANCELMODE,0,0);
  Check(TabIds(state)==reordered && state.dragTarget==-1 && !state.dragging && GetCapture()!=tabs,"Cancelling a drag clears its marker and leaves order intact");
  SendMessageW(tabs,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(first.left+18,15));
  SendMessageW(tabs,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(last.left+18,15));
  SendMessageW(tabs,WM_LBUTTONUP,0,MAKELPARAM(last.left+18,area.bottom+10));
  Check(TabIds(state)==reordered,"Dropping outside the tab row cancels the move");
  POINT context{first.left+18,15};ClientToScreen(tabs,&context);
  popupAction=0;SendMessageW(tabs,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(tabs),MAKELPARAM(context.x,context.y));
  Check(!popupLeftEnabled && popupRightEnabled,"Tab context menu disables moves past the left edge");
  Command(owner,IDC_NOTES_TAB_RIGHT);Check(TabIds(state)==std::vector<uint64_t>({initial[2],initial[1],initial[0]}),"Move-right command changes the selected tab's order");
  Command(owner,IDC_NOTES_CLOSE_TAB);
  const auto closed=initial[1];size_t index=0;while(state.pages[index].id!=closed) ++index;
  NotesLibrary library{owner,&state};HWND manager=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&library));
  const auto row=std::find(library.results.begin(),library.results.end(),index)-library.results.begin();
  SendDlgItemMessageW(manager,IDC_NOTES_RESULTS,LB_SETCURSEL,row,0);Command(manager,IDOK);DestroyWindow(manager);
  Check(TabIds(state).back()==closed && state.pages[state.activePage].id==closed,"Opening a closed middle note through All notes appends its tab");
  const auto reopened=TabIds(state);index=state.activePage;
  Check(NotesOpenSavedPage(owner,state,index) && TabIds(state)==reopened,"Opening an already-open note activates it without moving its tab");
  Command(owner,IDC_NOTES_PIN);Check(TabIds(state).front()==closed && !NotesCanReorderTab(state,0,1),"Pinned tabs stay together and cannot be dragged into the ordinary group");
  Command(owner,IDC_NOTES_CLOSE_TAB);Command(owner,IDC_NOTES_REOPEN);
  Check(TabIds(state).front()==closed && state.pages[state.activePage].pinned,"Reopen-last-closed retains the pinned group");
  CloseClientNotesDialog(owner,state);owner=Open(L"Tab order QA");
  Check(TabIds(State(owner))==std::vector<uint64_t>({closed,initial[2],initial[0]}),"Tab order and pin grouping survive an application restart");
  CloseClientNotesDialog(owner,State(owner));

  Fixture(L"Tab stress QA");owner=Open(L"Tab stress QA");Command(owner,IDC_NOTES_CLOSE_ALL);auto &stress=State(owner);
  std::vector<uint64_t> expected;std::map<uint64_t,std::wstring> contents;
  for(unsigned n=0;n<64;++n) {
    const auto body=L"Unique stress document "+std::to_wstring(n);
    client_notes::NotePage page{L"Stress "+std::to_wstring(n),true,NotesPlainRtf(body),{}};
    if(!NotesAddPage(owner,stress,std::move(page))) throw std::runtime_error("Stress setup save");
    auto &saved=stress.pages.back();saved.pinned=n<8;saved.ticket=L"T-"+std::to_wstring(n);saved.tags={L"stress",std::to_wstring(n)};
    saved.status=n%5;stress.metadataChanged=true;expected.push_back(saved.id);contents[saved.id]=body;
  }
  RefreshNotesTabs(owner,stress);Save(owner);uint32_t random=0x173ab49u;
  const auto next=[&]() {random^=random<<13;random^=random>>17;random^=random<<5;return random;};
  for(unsigned cycle=0;cycle<320;++cycle) {
    const size_t from=next()%expected.size();const bool pinned=from<8;
    if(cycle%3==0) {
      size_t target=pinned ? next()%8 : 8+next()%56;
      if(target!=from && !NotesReorderTab(owner,stress,static_cast<int>(from),static_cast<int>(target))) throw std::runtime_error("Stress reorder save");
      const auto id=expected[from];expected.erase(expected.begin()+from);expected.insert(expected.begin()+target,id);
    } else {
      const auto id=expected[from];size_t page=0;while(stress.pages[page].id!=id) ++page;
      stress.activePage=page;NotesSelectAvailable(owner,stress);Command(owner,IDC_NOTES_CLOSE_TAB);
      expected.erase(expected.begin()+from);page=0;while(stress.pages[page].id!=id) ++page;
      if(!NotesOpenSavedPage(owner,stress,page)) throw std::runtime_error("Stress reopen save");
      expected.insert(expected.begin()+(pinned ? 7 : expected.size()),id);
    }
    if(TabIds(stress)!=expected || !HasActiveNote(stress) || NotesWindowText(Edit(owner))!=contents.at(stress.pages[stress.activePage].id)) throw std::runtime_error("Stress order or document identity diverged");
    for(size_t n=0;n<stress.pages.size();++n) if(contents.count(stress.pages[n].id) && (stress.baselines[n]!=stress.pages[n].rtf || stress.pages[n].tags.size()!=2 || stress.pages[n].ticket.empty())) {
      std::cerr<<"Stress cycle "<<cycle<<", page "<<n<<", baseline="<<(stress.baselines[n]==stress.pages[n].rtf)<<", tags="<<stress.pages[n].tags.size()<<", ticket="<<!stress.pages[n].ticket.empty()<<std::endl;
      throw std::runtime_error("Stress metadata or baseline diverged");
    }
  }
  Check(true,"320 deterministic reorder/close/reopen cycles with 64 notes retain order, content, metadata and pin groups");
  CloseClientNotesDialog(owner,stress);owner=Open(L"Tab stress QA");
  Check(TabIds(State(owner))==expected,"Overflow tab stress order persists after restart");auto &conflict=State(owner);
  const auto before=TabIds(conflict);const auto active=conflict.pages[conflict.activePage].id;Type(owner,L" unsaved");const auto draft=NotesWindowText(Edit(owner));
  auto newer=conflict.original.pages;newer.front().ticket=L"External writer";
  Check(client_notes::WriteNotebook(conflict.clientRoot,newer,conflict.original),"Concurrent writer creates tab-reorder save conflict");
  Check(!NotesReorderTab(owner,conflict,8,9) && TabIds(conflict)==before && conflict.pages[conflict.activePage].id==active && NotesWindowText(Edit(owner))==draft,"Save conflict prevents reorder while retaining the active draft and tab order");
  size_t closedIndex=0;while(conflict.pages[closedIndex].id==active) ++closedIndex;conflict.pages[closedIndex].open=false;RefreshNotesTabs(owner,conflict);
  const auto failedOrder=TabIds(conflict);
  Check(!NotesOpenSavedPage(owner,conflict,closedIndex) && !conflict.pages[closedIndex].open && TabIds(conflict)==failedOrder && NotesWindowText(Edit(owner))==draft,"Save conflict prevents reopening another tab without losing the draft");
  Check(client_notes::ReadNotebook(conflict.clientRoot).pages.front().ticket==L"External writer" && fs::exists(conflict.clientRoot/conflict.draftName),"Failed tab actions keep a recoverable draft and protect the newer notebook");DestroyWindow(owner);
}
static LRESULT CALLBACK TrayFixtureProc(HWND window,UINT message,WPARAM w,LPARAM l) {
  if (message==WM_APP_SHOW_LAUNCHER || message==WM_APP_LAUNCHER_TRAY || message==g_taskbarCreated ||
      (message==WM_SIZE && w==SIZE_MINIMIZED) || (message==WM_COMMAND && LOWORD(w)==IDM_CTX_MINIMIZE_TRAY)) return WndProc(window,message,w,l);
  return DefWindowProcW(window,message,w,l);
}
static void LauncherTrayTests() {
  WNDCLASSW type{};type.lpfnWndProc=TrayFixtureProc;type.hInstance=g_hInst;type.lpszClassName=L"ctSpaces tray fixture";RegisterClassW(&type);
  HWND window=CreateWindowW(type.lpszClassName,L"Tray QA",WS_OVERLAPPEDWINDOW,100,100,300,150,nullptr,nullptr,g_hInst,nullptr);
  g_minimizeToTray=false;ShowWindow(window,SW_SHOWNOACTIVATE);
  SendMessageW(window,WM_COMMAND,IDM_CTX_MINIMIZE_TRAY,0);
  Check(g_minimizeToTray && GetPrivateProfileIntW(L"user",L"minimize_to_tray",0,g_sConfigPath.c_str())==1,"Tray preference persists through config mutations");
  SendMessageW(window,WM_SIZE,SIZE_MINIMIZED,0);
  Check(g_launcherInTray && g_launcherTrayIcon && !IsWindowVisible(window),"Enabled minimize hides the launcher only after tray registration succeeds");
  Check(trayCommands.size()>=2 && trayCommands[trayCommands.size()-2]==NIM_ADD && trayCommands.back()==NIM_SETVERSION,"Tray icon uses the accessible version-4 callback contract");
  const auto events=trayCommands.size();SendMessageW(window,g_taskbarCreated,0,0);
  Check(g_launcherInTray && trayCommands.size()==events+2,"Explorer taskbar recreation restores the hidden launcher's icon");
  SendMessageW(window,WM_APP_LAUNCHER_TRAY,0,MAKELPARAM(NIN_KEYSELECT,1));
  Check(IsWindowVisible(window) && !g_launcherInTray && !g_launcherTrayIcon && trayCommands.back()==NIM_DELETE,"Keyboard activation restores the launcher and removes the tray icon");
  Check((GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)==0,"Foreground restoration does not leave the launcher always on top");
  trayAddWorks=false;SendMessageW(window,WM_SIZE,SIZE_MINIMIZED,0);
  Check(IsWindowVisible(window) && !g_launcherInTray,"Tray registration failure leaves the launcher accessible");trayAddWorks=true;
  trayVersionWorks=false;SendMessageW(window,WM_SIZE,SIZE_MINIMIZED,0);
  Check(IsWindowVisible(window) && !g_launcherTrayIcon && trayCommands.back()==NIM_DELETE,"Unsupported tray callback version rolls back registration safely");trayVersionWorks=true;
  HideLauncherInTray(window);SendMessageW(window,WM_APP_SHOW_LAUNCHER,0,0);
  Check(IsWindowVisible(window) && !g_launcherInTray,"A repeated launch restores a hidden tray launcher");
  SendMessageW(window,WM_COMMAND,IDM_CTX_MINIMIZE_TRAY,0);SendMessageW(window,WM_SIZE,SIZE_MINIMIZED,0);
  Check(!g_minimizeToTray && IsWindowVisible(window) && !g_launcherInTray,"Turning tray mode off retains ordinary minimize behavior");
  realTrayApi=true;const bool registered=HideLauncherInTray(window);
  ShowLauncherForeground(window);realTrayApi=false;
  Check(registered && IsWindowVisible(window) && !g_launcherTrayIcon,"Real Windows tray registration, version negotiation and removal succeed");DestroyWindow(window);
}
static unsigned closeRequests=0;
static LRESULT CALLBACK CloseRaceProc(HWND window,UINT message,WPARAM w,LPARAM l) {
  if(message==WM_CLOSE) {++closeRequests;return 0;} // Deliberate browser veto.
  return DefWindowProcW(window,message,w,l);
}
static void CloseAllLaunchRace() {
  Check(g_sessions.empty(),"Close-race fixture starts without client sessions");
  const auto pid=GetCurrentProcessId();g_sessions.push_back({L"Close race QA",BrowserKind::Edge,pid});
  answers.push_back(IDYES);RequestCloseAllClientsNormally();
  Check(g_bCloseAllClientsPending && g_closeAllAwaitingWindows.size()==1 && g_closeAllAwaitingWindows.front().pid==pid &&
        WaitForSingleObject(g_closeAllAwaitingWindows.front().process.get(),0)==WAIT_TIMEOUT,"Close all holds the tracked process identity while waiting for its first window");
  WNDCLASSW kind{};kind.lpfnWndProc=CloseRaceProc;kind.hInstance=g_hInst;kind.lpszClassName=L"ctSpaces close race fixture";RegisterClassW(&kind);
  HWND window=CreateWindowW(kind.lpszClassName,L"Browser close QA",WS_OVERLAPPEDWINDOW|WS_VISIBLE,50,50,250,100,nullptr,nullptr,g_hInst,nullptr);
  WndProc(g_hGui,WM_TIMER,kCloseAllCheckTimer,0);PumpPanelMessages();
  Check(closeRequests==1 && g_closeAllAwaitingWindows.empty(),"A late browser window receives the pending normal close request");
  for(int i=0;i<8;++i) WndProc(g_hGui,WM_TIMER,kCloseAllCheckTimer,0);
  PumpPanelMessages();Check(closeRequests==1 && IsWindow(window),"A browser close veto is respected without repeated close requests");
  OnSessionEnded(pid);Check(!g_bCloseAllClientsPending && g_closeAllAwaitingWindows.empty(),"Session exit clears the pending close state");DestroyWindow(window);
  g_sessions.push_back({L"Default",BrowserKind::Edge,pid});answers.push_back(IDYES);RequestCloseAllClientsNormally();
  Check(!g_bCloseAllClientsPending,"Default sessions are excluded from the rapid-launch close path");answers.clear();g_sessions.clear();
}
static void CodeBorderRegression() {
  Fixture(L"Code border QA");HWND owner=Open(L"Code border QA");Command(owner,IDC_NOTES_CLOSE_ALL);
  std::string rtf=NotesCodeRtf({1,L"Get-Printer\r\n    Select-Object Name"});
  const auto row=rtf.find("\\trowd");
  std::string lead="\\pard\\plain\\f0\\fs24 ";for(int i=0;i<12;++i) lead+="Before the code box\\par ";
  rtf.insert(row,lead);const auto tail=rtf.rfind('}');
  std::string after;for(int i=0;i<18;++i) after+="After the code box\\par ";rtf.insert(tail,after);
  Check(NotesAddPage(owner,State(owner),{L"Code border",true,rtf,{}}),"Code-border scroll fixture saves");
  const auto codeId=State(owner).pages[State(owner).activePage].id;
  SetWindowPos(owner,HWND_TOP,30,30,1120,960,SWP_SHOWWINDOW);PumpPanelMessages();
  const auto inspect=[&](const char *label) {
    HWND edit=Edit(owner);RECT area{};GetClientRect(edit,&area);PumpPanelMessages();
    HDC screen=GetDC(edit),dc=CreateCompatibleDC(screen);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=area.right;info.bmiHeader.biHeight=-area.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void *bits=nullptr;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,bitmap);
    BitBlt(dc,0,0,area.right,area.bottom,screen,0,0,SRCCOPY);GdiFlush();
    const auto pixel=[&](int x,int y){return static_cast<DWORD*>(bits)[y*area.right+x]&0xFFFFFF;};
    std::vector<int> borders;int left=-1,right=-1;
    for(int y=0;y<area.bottom;++y) {
      int run=0,start=0;for(int x=0;x<area.right;++x) {
        if(pixel(x,y)==RGB(128,128,128)) {if(!run)start=x;++run;}
        else {if(run>200) {borders.push_back(y);left=start;right=x-1;break;}run=0;}
      }
    }
    bool complete=borders.size()==3 && left>=0 && right<area.right;
    if(complete) {for(int y=borders.front();y<=borders.back();++y) complete=complete && pixel(left,y)==RGB(128,128,128) && pixel(right,y)==RGB(128,128,128);}
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(edit,screen);Check(complete,label);
  };
  inspect("Native code box has all four borders before scrolling");
  POINT position{0,180};SendMessageW(Edit(owner),EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&position));
  inspect("Scrolling preserves all code borders without stale lines at the old position");
  for(int i=0;i<24;++i) {SetWindowPos(owner,nullptr,0,0,1020+i%5*20,880+i%4*15,SWP_NOMOVE|SWP_NOZORDER);PumpPanelMessages();}
  inspect("Repeated native resize preserves the entire code-box outline");
  Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Another cached note");Save(owner);
  auto &state=State(owner);state.activePage=static_cast<size_t>(std::find_if(state.pages.begin(),state.pages.end(),[codeId](const auto &page){return page.id==codeId;})-state.pages.begin());Check(LoadNotesPage(owner,state),"Code note reopens from its cached editor");
  inspect("Cached tab reveal retains complete code borders");CloseClientNotesDialog(owner,state);
}
static void NotesHeaderPolish() {
  Fixture(L"Header polish QA");HWND owner=Open(L"Header polish QA");auto &state=State(owner);
  Check(NotesWindowText(GetDlgItem(owner,IDC_NOTES_STATUS)).empty(),"Opening saved notes does not show a permanent Saved label");
  HWND picker=GetDlgItem(owner,IDC_NOTES_SWITCH_CLIENT);
  Check(NotesWindowText(picker)==state.clientName,"Client selector exposes the client name without a text-arrow suffix");
  const UINT dpi=GetDpiForWindow(owner);const int height=ScaleByDpi(30,dpi);
  HDC screen=GetDC(picker),dc=CreateCompatibleDC(screen);
  for (int width : {96,180,290}) {
    const int pixels=ScaleByDpi(width,dpi);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=pixels;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void *bits=nullptr;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,bitmap);
    const auto paint=[&](const RECT &rect,UINT appearance) {
      DRAWITEMSTRUCT draw{ODT_BUTTON,IDC_NOTES_SWITCH_CLIENT,0,ODA_DRAWENTIRE,appearance,picker,dc,rect,0};
      if (!DrawNotesToolbarButton(draw,state)) throw std::runtime_error("Production Notes client picker renderer did not handle the control");
    };
    for (UINT appearance : {0u,static_cast<UINT>(ODS_HOTLIGHT|ODS_FOCUS),static_cast<UINT>(ODS_DISABLED)}) {
      RECT rect{0,0,pixels,height};SetWindowTextW(picker,L"");paint(rect,appearance);GdiFlush();
      const auto *buffer=static_cast<DWORD*>(bits);std::vector<DWORD> blank(buffer,buffer+pixels*height);
      for (const wchar_t *name : {L"rob",L"Research & Development",L"A very long client name that must truncate cleanly before the arrow"}) {
        SetWindowTextW(picker,name);paint(rect,appearance);GdiFlush();
        int first=pixels,changed=0;bool arrowUnchanged=true;
        for(int y=0;y<height;++y) for(int x=0;x<pixels;++x) if(buffer[y*pixels+x]!=blank[y*pixels+x]) {
          first=(std::min)(first,x);++changed;if(x>=pixels-ScaleByDpi(32,dpi)) arrowUnchanged=false;
        }
        Check(changed>0 && first>=ScaleByDpi(38,dpi) && first<ScaleByDpi(54,dpi),"Short, ampersand and long client names stay left-aligned after the client icon in normal, hover and disabled states");
        Check(arrowUnchanged,"Client names and ellipsis never intrude into the separate chevron area");
      }
    }
    SelectObject(dc,old);DeleteObject(bitmap);
  }
  DeleteDC(dc);ReleaseDC(picker,screen);
  const auto originalName=state.clientName;
  int shortWidth=0,longWidth=0;
  for(const wchar_t *name : {L"rob",L"Research & Development",L"A very long client name that must truncate cleanly before the arrow"}) {
    state.clientName=name;UpdateNotesToolbar(owner,state);LayoutClientNotes(owner,dpi);
    RECT button{},library{};GetWindowRect(picker,&button);GetWindowRect(GetDlgItem(owner,IDC_NOTES_LIBRARY),&library);
    const int width=button.right-button.left;
    if(state.clientName==L"rob") shortWidth=width;else longWidth=(std::max)(longWidth,width);
    Check(width>=ScaleByDpi(96,dpi) && width<=ScaleByDpi(290,dpi) && button.right<library.left,"Selector autosizes within sensible bounds without crowding All notes");
  }
  Check(shortWidth<=ScaleByDpi(110,dpi) && longWidth>shortWidth,"A short client name and its icon use a compact selector while longer names get more room");
  state.clientName=originalName;UpdateNotesToolbar(owner,state);LayoutClientNotes(owner,dpi);
  if(!HasActiveNote(state)) Command(owner,IDC_NOTES_ADD_TAB);
  Type(owner,L"Quiet autosave test");
  Check(NotesWindowText(GetDlgItem(owner,IDC_NOTES_STATUS)).empty(),"Ordinary pending edits do not display routine saving messages");
  SendMessageW(owner,WM_TIMER,1,0);
  Check(NotesWindowText(GetDlgItem(owner,IDC_NOTES_STATUS)).empty(),"Successful autosave does not show Saved, even briefly");
  Check(NotesPlain(client_notes::ReadNotebook(state.clientRoot).pages[state.activePage]).find(L"Quiet autosave test")!=std::wstring::npos,"Quiet autosave still commits the edit to the notebook");
  Save(owner);SetNotesStatus(owner,state);
  Check(NotesWindowText(GetDlgItem(owner,IDC_NOTES_STATUS)).empty(),"An unchanged save or toolbar refresh does not bring back Saved");
  state.detailsDrafts[state.pages[state.activePage].id]={L"0",L"",L"",L"invalid URL"};SetNotesStatus(owner,state);
  Check(NotesWindowText(GetDlgItem(owner,IDC_NOTES_STATUS))==L"Check details","Invalid details remain visible while routine autosave stays quiet");
  state.detailsDrafts.clear();SetNotesStatus(owner,state);
  Check(NotesWindowText(GetDlgItem(owner,IDC_NOTES_STATUS)).empty(),"Correcting details clears the problem indicator");
  Type(owner,L" pending");
  auto external=state.original.pages;external[state.activePage].name=L"Changed by another instance";
  Check(client_notes::WriteNotebook(state.clientRoot,external,state.original),"Concurrent-writer fixture changes the saved notebook safely");
  Check(!SaveClientNotesDraft(owner,state,false) && state.saveFailed,"A real write conflict retains the unsaved draft and reports failure");
  UpdateNotesToolbar(owner,state);SetNotesStatus(owner,state);
  Check(NotesWindowText(GetDlgItem(owner,IDC_NOTES_STATUS))==L"Could not save" && NotesWindowText(owner).find(L"Could not save")!=std::wstring::npos,"A real save failure remains visible across toolbar refreshes");
  DestroyWindow(owner);
}

static LRESULT CALLBACK ObserveNotesWindowReuse(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
  if(message==WM_DESTROY) ++*reinterpret_cast<unsigned *>(data);
  if(message==WM_NCDESTROY) RemoveWindowSubclass(window,ObserveNotesWindowReuse,id);
  return DefSubclassProc(window,message,w,l);
}
static void NotesClientSwitchPlacement() {
  Fixture(L"Placement A");Fixture(L"Placement B");
  const bool enabled=g_bUiEnabled;g_bUiEnabled=true;
  WritePrivateProfileStringW(L"notes_window",L"help_seen",L"1",g_sConfigPath.c_str());
  WritePrivateProfileStringW(L"notes_window",L"maximized",L"0",g_sConfigPath.c_str());
  ShowClientNotes(L"Placement A");Check(IsWindow(g_hNotesWindow),"Placement fixture opens through the production Notes entry point");
  HWND owner=g_hNotesWindow;if(!HasActiveNote(State(owner))) Command(owner,IDC_NOTES_ADD_TAB);
  const HWND originalWindow=owner,originalPicker=GetDlgItem(owner,IDC_NOTES_SWITCH_CLIENT);
  unsigned destroyed=0;SetWindowSubclass(owner,ObserveNotesWindowReuse,96,reinterpret_cast<DWORD_PTR>(&destroyed));
  Type(owner,L"Only belongs to client A");Save(owner);
  MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);
  SetWindowPos(owner,nullptr,monitor.rcWork.left+140,monitor.rcWork.top+110,980,620,SWP_NOZORDER|SWP_NOACTIVATE);
  RECT expected{};GetWindowRect(owner,&expected);
  for(int i=0;i<12;++i) {
    const bool clientB=i%2==0;ShowClientNotes(clientB ? L"Placement B":L"Placement A");owner=g_hNotesWindow;
    Check(owner==originalWindow && GetDlgItem(owner,IDC_NOTES_SWITCH_CLIENT)==originalPicker && !destroyed,"Client switching reuses the actual Notes window and header controls without destroying them");
    RECT actual{};GetWindowRect(owner,&actual);
    Check(EqualRect(&expected,&actual),"Repeated client switching preserves the exact moved position and size");
    Check(State(owner).clientName==(clientB ? L"Placement B":L"Placement A"),"The client changes without changing window placement");
    const bool hasA=NotesWindowText(Edit(owner)).find(L"Only belongs to client A")!=std::wstring::npos;
    Check(hasA==!clientB,"Switching clients preserves notebook ownership and loads only the chosen client's text");
  }
  ShowWindow(owner,SW_MAXIMIZE);WINDOWPLACEMENT maximized{sizeof(maximized)};GetWindowPlacement(owner,&maximized);
  ShowClientNotes(L"Placement B");owner=g_hNotesWindow;WINDOWPLACEMENT switched{sizeof(switched)};GetWindowPlacement(owner,&switched);
  Check(IsZoomed(owner),"Switching a maximized Notes window keeps it maximized");
  ShowClientNotes(L"Placement B");Check(IsZoomed(owner) && owner==g_hNotesWindow,"Opening the current client again keeps the same maximized Notes window");
  Check(EqualRect(&maximized.rcNormalPosition,&switched.rcNormalPosition),"Maximized switching retains the original restore position and size");
  ShowWindow(owner,SW_MINIMIZE);ShowClientNotes(L"Placement A");owner=g_hNotesWindow;
  Check(IsZoomed(owner),"Switching from a minimized maximized window restores its maximized state");
  ShowWindow(owner,SW_RESTORE);RECT normal{};GetWindowRect(owner,&normal);
  Check(EqualRect(&expected,&normal),"Restoring after client switches returns to the user's original position and size");
  Type(owner,L" unsaved draft");auto &state=State(owner);auto external=state.original.pages;external[state.activePage].name=L"Concurrent change";
  Check(client_notes::WriteNotebook(state.clientRoot,external,state.original),"Placement conflict fixture safely commits a concurrent change");
  ShowClientNotes(L"Placement B");RECT retained{};GetWindowRect(owner,&retained);
  Check(g_hNotesWindow==owner && State(owner).clientName==L"Placement A" && EqualRect(&normal,&retained),"A failed save cancels the client switch without closing or moving the current Notes window");
  DestroyWindow(owner);g_bUiEnabled=enabled;
}

static void CompactNotesActionRow() {
  Fixture(L"Action row QA");HWND owner=Open(L"Action row QA");Command(owner,IDC_NOTES_ADD_TAB);
  auto &state=State(owner);Type(owner,L"Keep this draft intact while laying out the action row.");
  const auto text=NotesWindowText(Edit(owner));const HWND editor=Edit(owner);
  for (UINT dpi : {96u,144u,192u}) for (int width : {900,1240,1500,900}) {
    SetWindowPos(owner,nullptr,0,0,ScaleByDpi(width,dpi),ScaleByDpi(600,dpi),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    LayoutClientNotes(owner,dpi);
    RECT area{};GetClientRect(owner,&area);RECT prior{};bool first=true;
    for (int id : {IDC_NOTES_FIND_TOGGLE,IDC_NOTES_SNIPPETS,IDC_NOTES_TEMPLATES,IDC_NOTES_WORKLOG,IDC_NOTES_FORMAT,IDC_NOTES_DETAILS}) {
      const HWND button=GetDlgItem(owner,id);RECT bounds{};GetWindowRect(button,&bounds);
      MapWindowPoints(nullptr,owner,reinterpret_cast<POINT *>(&bounds),2);
      Check((GetWindowLongPtrW(button,GWL_STYLE)&WS_VISIBLE) && bounds.left>=0 && bounds.right<=area.right &&
          (first || (bounds.top==prior.top && bounds.bottom==prior.bottom && bounds.left>prior.right)),
          "Every quick action fits in one separated row at minimum and wide sizes across DPI");
      const HDC dc=GetDC(button);const HGDIOBJ font=SelectObject(dc,GetOwnerDrawFont(button));
      for (const std::wstring &label : std::vector<std::wstring>{NotesWindowText(button)}) {
        SIZE extent{};GetTextExtentPoint32W(dc,label.c_str(),static_cast<int>(label.size()),&extent);
        Check(extent.cx<=bounds.right-bounds.left-ScaleByDpi(notes_editor_ui::HasActionIcon(id) ? 25 : 16,dpi),"Quick action icons and labels fit without truncating status or ticket captions");
      }
      SelectObject(dc,font);ReleaseDC(button,dc);prior=bounds;first=false;
    }
    RECT body{};
    auto bounds=[&](int id,RECT &rect) {GetWindowRect(GetDlgItem(owner,id),&rect);MapWindowPoints(nullptr,owner,reinterpret_cast<POINT *>(&rect),2);};
    bounds(IDC_CLIENT_NOTES_TEXT,body);
    RECT findAction{};bounds(IDC_NOTES_FIND_TOGGLE,findAction);
    Check(findAction.left==ScaleByDpi(24,dpi) && prior.bottom<body.top &&
        !GetDlgItem(owner,IDC_NOTES_NOTE_HEADING) && !IsWindowVisible(GetDlgItem(owner,IDC_NOTES_OPEN_TICKET)) &&
        !IsWindowVisible(GetDlgItem(owner,IDC_NOTES_TICKET_REFERENCE)),
        "Tabs supply the note title and unlinked notes have no duplicate ticket action or metadata row");
    Check(body.top==ScaleByDpi(132,dpi) && !state.formatVisible &&
        !(GetWindowLongPtrW(GetDlgItem(owner,IDC_NOTES_STYLE),GWL_STYLE)&WS_VISIBLE),
        "An unlinked note gains thirty-two more DIPs while formatting stays on demand");
    Check(body.left==ScaleByDpi(28,dpi) && body.right==area.right-ScaleByDpi(28,dpi) &&
        Edit(owner)==editor && NotesWindowText(editor)==text,"Compact layout preserves the full-width editor and existing draft");
    state.findVisible=true;LayoutClientNotes(owner,dpi);RECT search{};bounds(IDC_NOTES_FIND,search);bounds(IDC_CLIENT_NOTES_TEXT,body);
    Check(search.bottom<body.top,"Expanded search remains above the editor in the compact layout");
    state.findVisible=false;LayoutClientNotes(owner,dpi);
  }
  LayoutClientNotes(owner,GetDpiForWindow(owner));DestroyWindow(owner);
}

static void NotesTicketHeaderActions() {
  Fixture(L"Ticket reference QA");Fixture(L"Ticket reference empty");
  HWND owner=Open(L"Ticket reference QA");Command(owner,IDC_NOTES_ADD_TAB);ShowWindow(owner,SW_SHOW);
  auto &state=State(owner);const HWND editor=Edit(owner);
  const HWND link=GetDlgItem(owner,IDC_NOTES_OPEN_TICKET),reference=GetDlgItem(owner,IDC_NOTES_TICKET_REFERENCE);
  Type(owner,L"Keep the note while its ticket information changes.");Select(editor,5,13);
  std::string before;StreamClientNotesOut(editor,before);
  Check(!GetDlgItem(owner,IDC_NOTES_NOTE_HEADING),"The active tab is the note title; no large repeated Untitled heading remains");
  for (int kind=0;kind<5;++kind) {
    auto &page=state.pages[state.activePage];
    page.ticket=(kind==2 || kind==3) ? L"T20260921.0622" : L"";
    page.ticketUrl=kind>=3 ? L"https://tickets.example.test/T20260921.0622" : L"";
    page.tags=kind==1 ? std::vector<std::wstring>{L"printing",L"rollout"} : std::vector<std::wstring>{};
    state.metadataChanged=true;UpdateNotesToolbar(owner,state);
    for (UINT dpi:{96u,144u,192u}) for (int width:{900,1240}) {
      SetWindowPos(owner,nullptr,0,0,ScaleByDpi(width,dpi),ScaleByDpi(600,dpi),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);LayoutClientNotes(owner,dpi);
      Check(IsWindowVisible(link)==(kind>=3) && IsWindowVisible(reference)==(kind==2),
          "Only a valid ticket URL shows an open action; a number alone is read-only and an unlinked note has no placeholder");
      RECT body{};GetWindowRect(editor,&body);MapWindowPoints(nullptr,owner,reinterpret_cast<POINT *>(&body),2);
      Check(body.top==ScaleByDpi(kind==0 ? 132 : 164,dpi),"The ticket/tag line takes space only when metadata is supplied");
      std::string after;StreamClientNotesOut(editor,after);CHARRANGE range{};NotesSelection(editor,range);
      Check(editor==Edit(owner) && before==after && range.cpMin==5 && range.cpMax==13,
          "Adding and removing ticket metadata preserves the editor, rich text and selection");
    }
    if (kind<3) {
      SetFocus(editor);const auto launched=shellOpens;Command(owner,IDC_NOTES_OPEN_TICKET);
      SendMessageW(reference,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(8,8));SendMessageW(reference,WM_LBUTTONUP,0,MAKELPARAM(8,8));
      Check(!state.panel && shellOpens==launched && GetFocus()==editor,
          "An absent or read-only ticket cannot open Details, launch a browser or steal typing focus");
    }
    if (kind==1) Check(NotesWindowText(GetDlgItem(owner,IDC_NOTES_CONTEXT))==L"printing, rollout","Tags without a ticket have no orphaned leading separator");
  }
  LayoutClientNotes(owner,GetDpiForWindow(owner));
  state.pages[state.activePage].ticketUrl.clear();state.pages[state.activePage].ticket=L"T20260921.0622";UpdateNotesToolbar(owner,state);
  Command(owner,IDC_NOTES_DETAILS);HWND panel=state.panel;
  Check(panel && IsWindow(panel),"Details remains the header's editing control for ticket information");
  SetDlgItemTextW(panel,IDC_NOTES_INPUT2,L"https://tickets.example.test/T20260921.0622");SendMessageW(panel,WM_TIMER,1,0);
  Check(IsWindowVisible(link) && !IsWindowVisible(reference) && IsWindowEnabled(link),"Adding a URL in Details replaces the plain reference with its distinct open-link action");
  CloseNotesPanel(state);const auto launched=shellOpens;answers.push_back(IDNO);Command(owner,IDC_NOTES_OPEN_TICKET);
  Check(!state.panel && shellOpens==launched && lastNotice.find(L"outside client browser profiles")!=std::wstring::npos,
      "The linked reference opens browser confirmation rather than duplicating Details");
  Command(owner,IDC_NOTES_DETAILS);panel=state.panel;SetDlgItemTextW(panel,IDC_NOTES_INPUT2,L"");SetDlgItemTextW(panel,IDC_NOTES_INPUT1,L"");SendMessageW(panel,WM_TIMER,1,0);CloseNotesPanel(state);
  Check(!IsWindowVisible(link) && !IsWindowVisible(reference) && client_notes::ReadNotebook(state.clientRoot).pages[state.activePage].ticket.empty(),
      "Clearing ticket details removes both header controls and autosaves the change");
  Check(SwitchClientNotes(owner,state,L"Ticket reference empty",g_sDataDir/L"Sites/Ticket reference empty") &&
      NotesWindowText(link).empty() && NotesWindowText(reference).empty() && !IsWindowVisible(link) && !IsWindowVisible(reference),
      "Switching to an empty notebook clears the previous client's ticket header");
  DestroyWindow(owner);
}

static void NotesWorkStatusIndicator() {
  Fixture(L"Status indicator QA");Fixture(L"Status indicator other");
  HWND owner=Open(L"Status indicator QA");Command(owner,IDC_NOTES_ADD_TAB);
  ShowWindow(owner,SW_SHOW);SetFocus(Edit(owner));
  auto &state=State(owner);const HWND editor=Edit(owner),indicator=GetDlgItem(owner,IDC_NOTES_WORK_STATUS);
  const uint64_t savedNoteId=state.pages[state.activePage].id;
  Type(owner,L"Keep this note's text and selection while changing status.");Save(owner);Select(editor,5,14);
  std::string original;StreamClientNotesOut(editor,original);
  wchar_t className[32]{};GetClassNameW(indicator,className,static_cast<int>(std::size(className)));
  Check(!_wcsicmp(className,L"Static") && !(GetWindowLongPtrW(indicator,GWL_STYLE)&WS_TABSTOP) &&
      !GetDlgItem(owner,IDC_NOTES_METADATA),"The status is accessible static text, with no duplicate button or keyboard stop");
  for (unsigned status=0;status<=4;++status) {
    state.pages[state.activePage].status=status;state.metadataChanged=true;UpdateNotesToolbar(owner,state);
    for (UINT dpi:{96u,144u,192u}) for (int width:{900,1240}) {
      SetWindowPos(owner,nullptr,0,0,ScaleByDpi(width,dpi),ScaleByDpi(600,dpi),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
      LayoutClientNotes(owner,dpi);
      Check(IsWindowVisible(indicator)==(status!=0),"No status hides the indicator; every assigned status is visible across window sizes and DPI");
      if (status) {
        RECT label{},details{},format{};GetWindowRect(indicator,&label);GetWindowRect(GetDlgItem(owner,IDC_NOTES_DETAILS),&details);
        GetWindowRect(GetDlgItem(owner,IDC_NOTES_FORMAT),&format);
        Check(label.left>format.right && details.left-label.right==ScaleByDpi(16,dpi) &&
            label.top==details.top && label.bottom==details.bottom,"The compact status keeps clear space beside Details and the quick actions");
        HDC dc=GetDC(indicator);const HGDIOBJ font=SelectObject(dc,state.uiFont);SIZE extent{};
        const auto text=NotesWindowText(indicator);GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&extent);
        SelectObject(dc,font);ReleaseDC(indicator,dc);
        Check(text==NotesWorkStatusLabel(status) && extent.cx<=label.right-label.left-ScaleByDpi(26,dpi) &&
            label.right-label.left<=extent.cx+ScaleByDpi(30,dpi),"The full status word fits beside its icon without a fixed bulky box");
      }
      CHARRANGE range{};NotesSelection(editor,range);std::string current;StreamClientNotesOut(editor,current);
      Check(Edit(owner)==editor && current==original && range.cpMin==5 && range.cpMax==14,
          "Changing status and resizing preserve the same editor, rich text and selection");
    }
  }
  LayoutClientNotes(owner,GetDpiForWindow(owner));SetFocus(editor);
  SendMessageW(indicator,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(8,8));SendMessageW(indicator,WM_LBUTTONUP,0,MAKELPARAM(8,8));
  Check(!state.panel && GetFocus()==editor,"Clicking the read-only status does not open a duplicate editing panel or take keyboard focus");
  Command(owner,IDC_NOTES_DETAILS);HWND panel=state.panel;
  Check(panel && IsWindow(panel),"Details remains the direct control for editing status");
  SendDlgItemMessageW(panel,IDC_NOTES_STATUS_PICK,CB_SETCURSEL,2,0);SendMessageW(panel,WM_TIMER,1,0);
  Check(NotesWindowText(indicator)==L"Waiting" && IsWindowVisible(indicator) &&
      client_notes::ReadNotebook(state.clientRoot).pages[state.activePage].status==2,"Changing status in Details immediately updates the indicator and autosaves");
  NMTTDISPINFOW tooltip{};tooltip.hdr.hwndFrom=state.toolbarTooltip;tooltip.hdr.idFrom=reinterpret_cast<UINT_PTR>(indicator);tooltip.hdr.code=TTN_GETDISPINFOW;
  SendMessageW(owner,WM_NOTIFY,0,reinterpret_cast<LPARAM>(&tooltip));
  Check(tooltip.lpszText && std::wstring(tooltip.lpszText).find(L"Waiting on client")!=std::wstring::npos &&
      std::wstring(tooltip.lpszText).find(L"Details")!=std::wstring::npos,"Status hover explains its full meaning and where to edit it");
  SendDlgItemMessageW(panel,IDC_NOTES_STATUS_PICK,CB_SETCURSEL,0,0);SendMessageW(panel,WM_TIMER,1,0);CloseNotesPanel(state);
  Check(!IsWindowVisible(indicator) && NotesWindowText(indicator).empty(),"Choosing No status clears the indicator immediately");
  state.pages[state.activePage].status=4;state.metadataChanged=true;UpdateNotesToolbar(owner,state);Save(owner);
  Command(owner,IDC_NOTES_ADD_TAB);
  Check(!IsWindowVisible(indicator) && NotesWindowText(indicator).empty(),"A new note does not inherit the previous note's status display");
  Check(SwitchClientNotes(owner,state,L"Status indicator other",g_sDataDir/L"Sites/Status indicator other") &&
      !IsWindowVisible(indicator) && NotesWindowText(indicator).empty(),"An empty client notebook clears the status while retaining the Notes window");
  DestroyWindow(owner);owner=Open(L"Status indicator QA");
  auto &reopened=State(owner);const auto saved=std::find_if(reopened.pages.begin(),reopened.pages.end(),[&](const auto &page){return page.id==savedNoteId;});
  Check(saved!=reopened.pages.end() && saved->status==4 &&
      NotesOpenSavedPage(owner,reopened,static_cast<size_t>(saved-reopened.pages.begin())),"Reopen the specific saved note rather than a different tab left by empty-draft cleanup");
  Check(NotesWindowText(GetDlgItem(owner,IDC_NOTES_WORK_STATUS))==L"Resolved" &&
      (GetWindowLongPtrW(GetDlgItem(owner,IDC_NOTES_WORK_STATUS),GWL_STYLE)&WS_VISIBLE),"The saved note's status is shown again when its notebook reopens");
  DestroyWindow(owner);
}

static void NotesFormattingOnDemand() {
  Fixture(L"Formatting QA");HWND owner=Open(L"Formatting QA");Command(owner,IDC_NOTES_ADD_TAB);
  ShowWindow(owner,SW_SHOW);SetFocus(Edit(owner));
  auto &state=State(owner);HWND editor=Edit(owner);
  Type(owner,L"First line\r\n\r\nSelect these words to format.\r\n\r\nLast line");Save(owner);
  const auto text=NotesWindowText(editor);std::string original;StreamClientNotesOut(editor,original);
  auto select=[&](LONG start,LONG end) {
    SetFocus(editor);Select(editor,start,end);SendMessageW(owner,WM_TIMER,kNotesFormatTimer,0);
  };
  select(14,20);
  Check(state.formatVisible && !state.formatExplicit && IsWindowVisible(state.formatSurface),"A text selection opens the native contextual formatting palette");
  std::string opened;StreamClientNotesOut(editor,opened);
  Check(opened==original,"Showing contextual formatting does not rewrite saved text or formatting");
  RECT before{};GetWindowRect(editor,&before);
  CHARRANGE range{};NotesSelection(editor,range);
  SetFocus(GetDlgItem(owner,IDC_NOTES_BOLD));
  Command(owner,IDC_NOTES_BOLD);
  CHARRANGE after{};NotesSelection(editor,after);CHARFORMAT2W format{sizeof(format)};
  SendMessageW(editor,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
  Check(range.cpMin==after.cpMin && range.cpMax==after.cpMax && (format.dwEffects&CFE_BOLD),"Clicking Bold in the palette preserves the selected text and formats that range");
  Check(state.formatVisible && GetFocus()==editor,"Applying formatting returns focus to the same editor without closing the palette");
  for(const auto [id,effect]:{std::pair{IDC_NOTES_ITALIC,CFE_ITALIC},{IDC_NOTES_UNDERLINE,CFE_UNDERLINE}}) {
    SetFocus(GetDlgItem(owner,id));Command(owner,id);
    SendMessageW(editor,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
    Check((format.dwEffects&effect) && GetFocus()==editor,"Focused formatting buttons apply once when returning to the editor");
    SetFocus(GetDlgItem(owner,id));Command(owner,id);
    SendMessageW(editor,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
    Check(!(format.dwEffects&effect),"Focused formatting buttons also toggle off exactly once");
  }
  Command(owner,IDC_NOTES_HIGHLIGHT);SendMessageW(editor,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
  Check(format.crBackColor==state.highlightColor && state.formatVisible,"Highlight works on the palette's retained selection");
  Check(IsWindowVisible(GetDlgItem(owner,IDC_NOTES_HIGHLIGHT_COLOR)),"Highlight color picker remains directly accessible in the palette");
  popupAction=2;const auto menus=popupCalls;Command(owner,IDC_NOTES_HIGHLIGHT_COLOR);
  SendMessageW(editor,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
  Check(popupCalls==menus+1 && state.highlightColor==RGB(164,224,141) && format.crBackColor==state.highlightColor && state.formatVisible,
      "Highlight dropdown opens the color menu and applies the chosen color to the retained selection");
  SetFocus(GetDlgItem(owner,IDC_NOTES_SIZE));SendDlgItemMessageW(owner,IDC_NOTES_SIZE,CB_SETCURSEL,4,0);
  SendMessageW(owner,WM_COMMAND,MAKEWPARAM(IDC_NOTES_SIZE,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(owner,IDC_NOTES_SIZE)));
  SendMessageW(editor,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
  Check(format.yHeight==320 && state.formatVisible,"Font size selector changes the selected text without disturbing the palette");
  RECT unchanged{};GetWindowRect(editor,&unchanged);
  Check(EqualRect(&before,&unchanged) && Edit(owner)==editor,"Opening and using the palette never moves or replaces the editor");
  SendMessageW(editor,WM_KEYDOWN,VK_ESCAPE,0);
  Check(IsWindow(owner) && !state.formatVisible,"Escape dismisses contextual formatting while keeping Notes open");
  SendMessageW(owner,WM_TIMER,kNotesFormatTimer,0);
  Check(!state.formatVisible,"The same selection does not immediately reopen a dismissed palette");
  select(21,26);Check(state.formatVisible,"A new selection makes formatting available again");
  SendMessageW(owner,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(12,110));
  Check(!state.formatVisible,"Clicking the surrounding Notes surface dismisses formatting");
  select(26,30);SetFocus(GetDlgItem(owner,IDC_NOTES_MORE));
  Check(!state.formatVisible,"Moving focus to another action dismisses formatting");
  SetFocus(editor);Select(editor,0,0);Command(owner,IDC_NOTES_FORMAT);
  Check(state.formatVisible && state.formatExplicit,"Format opens the complete palette at an empty selection for typing");
  const HWND sizePicker=GetDlgItem(owner,IDC_NOTES_SIZE);SetFocus(sizePicker);
  SendMessageW(sizePicker,CB_SHOWDROPDOWN,TRUE,0);SendMessageW(sizePicker,WM_KEYDOWN,VK_ESCAPE,0);
  Check(!SendMessageW(sizePicker,CB_GETDROPPEDSTATE,0,0) && state.formatVisible,"Escape closes an open font dropdown before dismissing the formatting palette");
  SendMessageW(sizePicker,WM_KEYDOWN,VK_ESCAPE,0);
  Check(!state.formatVisible && GetFocus()==editor && IsWindow(owner),"A second Escape returns from formatting to writing without closing Notes");
  Command(owner,IDC_NOTES_FORMAT);
  const auto checkPalette=[&](UINT dpi) {
    RECT palette{},body{};GetWindowRect(state.formatSurface,&palette);GetWindowRect(editor,&body);
    Check(palette.left>=body.left && palette.right<=body.right && palette.top>=body.top && palette.bottom<=body.bottom,"Palette stays inside the editor viewport at the selection and caret edges");
    RECT prior{};bool first=true;
    for(int id=IDC_NOTES_STYLE;id<=IDC_NOTES_CLEAR;++id) {
      RECT bounds{};GetWindowRect(GetDlgItem(owner,id),&bounds);
      Check(IsWindowVisible(GetDlgItem(owner,id)) && bounds.left>=palette.left && bounds.right<=palette.right &&
          bounds.top>=palette.top && bounds.bottom<=palette.bottom && (first || bounds.left>=prior.right),
          "All existing formatting tools remain visible, separated and inside the floating palette");
      prior=bounds;first=false;
    }
    Check(palette.bottom-palette.top==ScaleByDpi(48,dpi),"Palette is compact at the requested DPI");
  };
  for(UINT dpi:{96u,144u,192u}) for(int width:{900,1240}) {
    HideNotesFormat(owner,state);SetWindowPos(owner,nullptr,0,0,ScaleByDpi(width,dpi),ScaleByDpi(600,dpi),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    LayoutClientNotes(owner,dpi);
    for(LONG at:{0L,12L,static_cast<LONG>(text.size())}) {
      Select(editor,at,at);ShowNotesFormat(owner,state,true);PositionNotesFormat(owner,state,dpi);checkPalette(dpi);
    }
    HideNotesFormat(owner,state);
  }
  LayoutClientNotes(owner,GetDpiForWindow(owner));Select(editor,14,20);Command(owner,IDC_NOTES_FORMAT);
  SendMessageW(editor,WM_VSCROLL,SB_LINEDOWN,0);Check(!state.formatVisible,"Scrolling dismisses the palette instead of leaving it over unrelated text");
  Command(owner,IDC_NOTES_FORMAT);SendMessageW(owner,WM_SIZE,SIZE_RESTORED,0);
  Check(!state.formatVisible,"Resize hides transient formatting without rebuilding the editor");
  SetNotesSaveFailure(owner,state,true);RECT problem{},body{};
  GetWindowRect(GetDlgItem(owner,IDC_NOTES_STATUS),&problem);GetWindowRect(editor,&body);
  Check(IsWindowVisible(GetDlgItem(owner,IDC_NOTES_STATUS)) && problem.bottom<body.top,
      "An actual save problem gets its own visible strip above the editor");
  SetNotesSaveFailure(owner,state,false);
  Check(!IsWindowVisible(GetDlgItem(owner,IDC_NOTES_STATUS)),"Resolving the save problem removes the strip and restores writing space");
  Command(owner,IDC_NOTES_FORMAT);Command(owner,IDC_NOTES_FIND_TOGGLE);
  Check(!state.formatVisible && state.findVisible,"Find closes floating formatting and opens a clear search row");Command(owner,IDC_NOTES_FIND_CLOSE);
  Command(owner,IDC_NOTES_FORMAT);Command(owner,IDC_NOTES_ADD_TAB);
  Check(!state.formatVisible && HasActiveNote(state),"A new note starts with a clean editor and no stale palette");
  SetFocus(Edit(owner));Type(owner,L"Keyboard formatting still works.");Select(Edit(owner),0,8);HideNotesFormat(owner,state);
  BYTE keys[256]{},pressed[256]{};GetKeyboardState(keys);memcpy(pressed,keys,sizeof(keys));pressed[VK_CONTROL]=0x80;SetKeyboardState(pressed);
  SendMessageW(Edit(owner),WM_KEYDOWN,L'B',0);SetKeyboardState(keys);
  SendMessageW(Edit(owner),EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
  Check(format.dwEffects&CFE_BOLD,"Existing keyboard formatting command works with the palette hidden");
  Command(owner,IDC_NOTES_FORMAT);
  Check(SwitchClientNotes(owner,state,L"Destination QA",g_sDataDir/L"Sites/Destination QA") && !state.formatVisible && g_hNotesWindow==owner,"Client switching keeps the Notes window and resets contextual formatting");
  DestroyWindow(owner);
}

struct NotesTabMutationCounts {unsigned resets=0,deletes=0,inserts=0;};
static LRESULT CALLBACK ObserveNotesTabMutation(HWND tabs,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
  auto &counts=*reinterpret_cast<NotesTabMutationCounts *>(data);
  if(message==TCM_DELETEALLITEMS) ++counts.resets;
  if(message==TCM_DELETEITEM) ++counts.deletes;
  if(message==TCM_INSERTITEMW) ++counts.inserts;
  if(message==WM_NCDESTROY) RemoveWindowSubclass(tabs,ObserveNotesTabMutation,id);
  return DefSubclassProc(tabs,message,w,l);
}
struct NotesTabTooltipObservation {
  HWND tabs=nullptr,tooltip=nullptr;
  unsigned pops=0,mutations=0;
  bool popped=false,mutationBeforePop=false;
};
static LRESULT CALLBACK ObserveNotesTabTooltip(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
  auto &observed=*reinterpret_cast<NotesTabTooltipObservation *>(data);
  if(window==observed.tooltip && message==TTM_POP) {++observed.pops;observed.popped=true;}
  if(window==observed.tabs && (message==TCM_INSERTITEMW || message==TCM_DELETEITEM || message==TCM_DELETEALLITEMS || message==TCM_SETITEMW)) {
    ++observed.mutations;if(!observed.popped) observed.mutationBeforePop=true;
  }
  if(message==WM_NCDESTROY) RemoveWindowSubclass(window,ObserveNotesTabTooltip,id);
  return DefSubclassProc(window,message,w,l);
}
static void NotesTabTooltipLifetime() {
  Fixture(L"Tab tooltip QA");const auto root=g_sDataDir/L"Sites/Tab tooltip QA";
  std::vector<client_notes::NotePage> pages;
  for(const wchar_t *name:{L"Printer rollout",L"Work log",L"Contacts",L"Untitled"}) {
    client_notes::NotePage page{name,true,NotesPlainRtf(name),{}};
    page.id=pages.size()+1;page.active=page.id==4;pages.push_back(std::move(page));
  }
  Check(client_notes::WriteNotebook(root,pages,client_notes::ReadNotebook(root)),"Tooltip fixture saves four distinct notes");
  HWND owner=Open(L"Tab tooltip QA");auto &state=State(owner);HWND tabs=GetDlgItem(owner,IDC_NOTES_TABS);
  HWND tooltip=TabCtrl_GetToolTips(tabs);Check(tooltip && IsWindow(tooltip),"The production native tab tooltip exists");
  NotesTabTooltipObservation observed{tabs,tooltip};
  SetWindowSubclass(tabs,ObserveNotesTabTooltip,94,reinterpret_cast<DWORD_PTR>(&observed));
  SetWindowSubclass(tooltip,ObserveNotesTabTooltip,94,reinterpret_cast<DWORD_PTR>(&observed));
  const auto caption=[&](size_t index) {
    NMTTDISPINFOW info{};info.hdr={tooltip,index,TTN_GETDISPINFOW};
    SendMessageW(owner,WM_NOTIFY,0,reinterpret_cast<LPARAM>(&info));
    return info.lpszText ? std::wstring(info.lpszText) : std::wstring{};
  };
  const auto reset=[&] {observed={tabs,tooltip};};
  Check(caption(3).starts_with(L"Untitled\r\n"),"A tooltip initially reads the fourth note's name");
  reset();Check(NotesReorderTab(owner,state,3,1),"The active fourth note moves into the second tab");
  Check(observed.pops && observed.mutations && !observed.mutationBeforePop && state.tabTooltip.empty(),"Reordering dismisses the native tooltip and clears its caption before reusing tool indices");
  Check(caption(1).starts_with(L"Untitled\r\n"),"The moved tab's next tooltip resolves its new index");
  reset();Command(owner,IDC_NOTES_CLOSE_TAB);
  Check(observed.pops && observed.mutations && !observed.mutationBeforePop && state.tabTooltip.empty(),"Closing a moved tab dismisses its tooltip before the adjacent tab reuses its index");
  Check(TabCtrl_GetItemCount(tabs)==3 && caption(1).starts_with(L"Work log\r\n"),"The reused second index now returns Work log instead of the closed Untitled caption");
  reset();state.pages[state.visiblePages[1]].name=L"Work log renamed";RefreshNotesTabs(owner,state);
  Check(observed.pops && observed.mutations && !observed.mutationBeforePop && state.tabTooltip.empty(),"Renaming dismisses the previous native caption before changing the tab label");
  Check(caption(1).starts_with(L"Work log renamed\r\n"),"A fresh tooltip request uses the renamed note");
  reset();RefreshNotesTabs(owner,state);
  Check(observed.pops && !observed.mutations && !observed.mutationBeforePop,"Refreshing tooltip lifetime does not rebuild an unchanged tab strip");
  RemoveWindowSubclass(tabs,ObserveNotesTabTooltip,94);RemoveWindowSubclass(tooltip,ObserveNotesTabTooltip,94);
  DestroyWindow(owner);
}

static void NotesTabOverflowTransition(const fs::path &output={}) {
  Fixture(L"Tab overflow QA");HWND owner=Open(L"Tab overflow QA");Command(owner,IDC_NOTES_CLOSE_ALL);
  auto &state=State(owner);const UINT dpi=GetDpiForWindow(owner);
  SetWindowPos(owner,HWND_TOP,40,40,ScaleByDpi(1046,dpi),ScaleByDpi(620,dpi),SWP_SHOWWINDOW);
  HWND tabs=GetDlgItem(owner,IDC_NOTES_TABS);
  RECT picker{},strip{};GetWindowRect(GetDlgItem(owner,IDC_NOTES_SWITCH_CLIENT),&picker);GetWindowRect(tabs,&strip);
  Check(picker.left==strip.left,"Client picker aligns with the tab strip rather than sitting farther left");
  NotesTabMutationCounts mutations;SetWindowSubclass(tabs,ObserveNotesTabMutation,95,reinterpret_cast<DWORD_PTR>(&mutations));
  for(int count=1;count<=12;++count) {
    Command(owner,IDC_NOTES_ADD_TAB);Type(owner,(L"Unique note "+std::to_wstring(count)).c_str());
    PumpPanelMessages();
    RECT row{},selected{};GetClientRect(tabs,&row);TabCtrl_GetItemRect(tabs,TabCtrl_GetCurSel(tabs),&selected);
    HWND arrows=FindWindowExW(tabs,nullptr,UPDOWN_CLASSW,nullptr);RECT navigation{};
    const bool overflow=arrows && (GetWindowLongPtrW(arrows,GWL_STYLE)&WS_VISIBLE);
    if(overflow) {GetWindowRect(arrows,&navigation);MapWindowPoints(nullptr,tabs,reinterpret_cast<POINT *>(&navigation),2);}
    const int visibleRight=overflow ? navigation.left : row.right;
    std::cout << "Tab transition: count=" << count << ", selected=" << TabCtrl_GetCurSel(tabs)
      << ", left=" << selected.left << ", right=" << selected.right << ", viewport=" << visibleRight << ", overflow=" << overflow << std::endl;
    if(!output.empty() && (count==8 || count==9)) {
      fs::create_directories(output);RedrawWindow(owner,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
      RECT bounds{};GetWindowRect(owner,&bounds);HDC screen=GetDC(owner),dc=CreateCompatibleDC(screen);
      HBITMAP bitmap=CreateCompatibleBitmap(screen,bounds.right-bounds.left,bounds.bottom-bounds.top);const HGDIOBJ old=SelectObject(dc,bitmap);
      Check(PrintWindow(owner,dc,2)!=FALSE,"Capture the real Notes window at the first tab overflow boundary");
      SelectObject(dc,old);const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
      {Gdiplus::Bitmap snapshot(bitmap,nullptr);Check(snapshot.Save((output/(std::to_wstring(count)+L"-tabs.png")).c_str(),&png,nullptr)==Gdiplus::Ok,"Save the overflow transition capture");}
      DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(owner,screen);
    }
    Check(TabCtrl_GetItemCount(tabs)==count && TabCtrl_GetCurSel(tabs)==count-1,
        "Adding a note retains all open tabs and selects the new tab");
    Check(selected.left>=0 && selected.right<=visibleRight,
        "The newly selected tab and its close button stay fully visible beside overflow navigation");
    Check(!overflow || selected.right>visibleRight-ScaleByDpi(118,dpi),
        "Overflow scrolls only enough to reveal the new tab rather than jumping it to the far left");
  }
  Check(mutations.resets==0 && mutations.deletes==0 && mutations.inserts==12,
      "Adding twelve tabs crosses overflow without deleting or rebuilding any existing tab");
  HWND arrows=FindWindowExW(tabs,nullptr,UPDOWN_CLASSW,nullptr);RECT navigation{};GetClientRect(arrows,&navigation);
  const LPARAM left=MAKELPARAM(navigation.right/4,navigation.bottom/2);
  SendMessageW(arrows,WM_LBUTTONDOWN,MK_LBUTTON,left);SendMessageW(arrows,WM_LBUTTONUP,0,left);
  RECT before{},after{};TabCtrl_GetItemRect(tabs,0,&before);const auto order=TabIds(state);const auto active=state.pages[state.activePage].id;
  const HWND editor=Edit(owner);const auto content=NotesWindowText(editor);const auto scroll=SendMessageW(arrows,UDM_GETPOS32,0,0);
  for(int repeat=0;repeat<16;++repeat) RefreshNotesTabs(owner,state);
  TabCtrl_GetItemRect(tabs,0,&after);
  Check(EqualRect(&before,&after) && SendMessageW(arrows,UDM_GETPOS32,0,0)==scroll && mutations.resets==0 && mutations.deletes==0 && mutations.inserts==12,
      "Refreshing an unchanged notebook preserves a manually scrolled viewport without replacing tabs");
  Check(TabIds(state)==order && state.pages[state.activePage].id==active && Edit(owner)==editor && NotesWindowText(editor)==content,
      "Scrolling and refresh retain every note, its order, active identity and cached editor");
  Command(owner,IDC_NOTES_CLOSE_TAB);
  Check(TabCtrl_GetItemCount(tabs)==11 && mutations.resets==0 && mutations.deletes==1,
      "Closing a tab in an overflowing row removes only that tab");
  Command(owner,IDC_NOTES_REOPEN);
  Check(TabCtrl_GetItemCount(tabs)==12 && TabIds(state)==order && state.pages[state.activePage].id==active,
      "Reopening the overflow tab restores its note and selects it without losing the other tabs");
  Save(owner);RemoveWindowSubclass(tabs,ObserveNotesTabMutation,95);
  CloseClientNotesDialog(owner,state);
  owner=Open(L"Tab overflow QA");Check(TabIds(State(owner))==order && State(owner).pages[State(owner).activePage].id==active,
      "Overflow tab order and active note survive reopening Notes");DestroyWindow(owner);
}

static void NotesButtonHover() {
  Fixture(L"Button hover QA");HWND owner=Open(L"Button hover QA");
  Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Keep this note while hovering.");Select(Edit(owner),5,14);
  const HWND editor=Edit(owner);const HWND focus=GetFocus();std::string before;StreamClientNotesOut(editor,before);
  RECT original{};GetWindowRect(editor,&original);
  const auto savedColors=g_themeColors;const bool savedDark=g_bThemeIsDark;LoadBakedThemesFromHeader();
  for(const wchar_t *name:{L"Dark - Gothic",L"Marine"}) {
    const auto theme=std::find_if(g_aCustomThemes.begin(),g_aCustomThemes.end(),[&](const ThemeEntry &entry){return entry.name==name;});
    Check(theme!=g_aCustomThemes.end(),"Hover regression uses a real dark or light theme");
    g_themeColors=theme->colors;g_bThemeIsDark=theme->dark;UpdateThemeBrushes();RefreshNotesForTheme();
    DWORD moreBorder=0;
    for(int id:{IDC_NOTES_MORE,IDC_NOTES_HELP,IDC_NOTES_DETAILS,IDC_NOTES_TRASH,IDC_NOTES_LIBRARY,IDC_NOTES_FIND_TOGGLE}) {
      HWND button=GetDlgItem(owner,id);DWORD_PTR data=0;
      Check(GetWindowSubclass(button,ButtonHotSubclassProc,1,&data)!=FALSE,"Header and Details controls receive the actual mouse hover tracking");
      RECT area{};GetClientRect(button,&area);HDC screen=GetDC(button),dc=CreateCompatibleDC(screen);
      HBITMAP bitmap=CreateCompatibleBitmap(screen,area.right,area.bottom);const HGDIOBJ old=SelectObject(dc,bitmap);
      const auto paint=[&](UINT appearance=0u) {
        DRAWITEMSTRUCT draw{ODT_BUTTON,static_cast<UINT>(id),0,ODA_DRAWENTIRE,appearance,button,dc,area,0};
        SendMessageW(owner,WM_DRAWITEM,id,reinterpret_cast<LPARAM>(&draw));
        GdiFlush();return std::pair{GetPixel(dc,area.right/2,ScaleByDpi(3,GetDpiForWindow(owner))),GetPixel(dc,area.right/2,0)};
      };
      g_hHotButton=nullptr;const auto normal=paint();
      if(id==IDC_NOTES_MORE) moreBorder=normal.second;
      if(id==IDC_NOTES_TRASH) Check(normal.second==moreBorder && normal.second!=normal.first,
          "Trash retains the same visible resting outline as the other header buttons");
      SendMessageW(button,WM_MOUSEMOVE,0,MAKELPARAM(area.right/2,area.bottom/2));
      Check(g_hHotButton==button,"Mouse entry marks the actual control hot without a synthetic owner-draw flag");
      const auto hovered=paint();
      Check(hovered.first!=normal.first && hovered.second!=normal.second,
          "More, Help, Details and neighboring actions visibly change face and outline on hover");
      SendMessageW(button,WM_MOUSELEAVE,0,0);
      Check(g_hHotButton!=button && paint()==normal,"Mouse exit restores the exact resting button appearance");
      const auto disabled=paint(ODS_DISABLED);g_hHotButton=button;
      Check(paint(ODS_DISABLED)==disabled,"Disabled buttons never pick up an enabled hover appearance");
      SendMessageW(button,WM_MOUSELEAVE,0,0);
      SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(button,screen);
    }
  }
  g_themeColors=savedColors;g_bThemeIsDark=savedDark;UpdateThemeBrushes();RefreshNotesForTheme();
  std::string after;StreamClientNotesOut(editor,after);CHARRANGE selected{};NotesSelection(editor,selected);RECT body{};GetWindowRect(editor,&body);
  Check(before==after && selected.cpMin==5 && selected.cpMax==14 && Edit(owner)==editor && GetFocus()==focus && EqualRect(&original,&body),
      "Hover feedback preserves note content, selection, focus and writing space");
  g_hHotButton=nullptr;DestroyWindow(owner);
}

static void NotesLauncherStyle() {
  Fixture(L"Style icon QA");Fixture(L"Style fallback QA");
  const auto iconPath=g_sDataDir/L"Sites/Style icon QA/client.ico";
  fs::copy_file(fs::current_path()/L"icons/browser.ico",iconPath,fs::copy_options::overwrite_existing);
  HWND owner=Open(L"Style icon QA");auto &state=State(owner);Command(owner,IDC_NOTES_ADD_TAB);
  Type(owner,L"The writing area stays unchanged.");Save(owner);Select(Edit(owner),4,11);
  Check(state.clientIcon && state.clientIconName==state.clientName && state.clientIcon!=GetClientIconForUi(state.clientName,ScaleByDpi(18,GetDpiForWindow(owner))),
      "Notes owns a copy of the client's existing launcher icon");
  const auto icon=state.clientIcon;const HWND editor=Edit(owner);std::string before;StreamClientNotesOut(editor,before);
  RECT original{};GetWindowRect(editor,&original);
  for(int i=0;i<40;++i) {LayoutClientNotes(owner,GetDpiForWindow(owner));UpdateNotesToolbar(owner,state);}
  RECT afterLayout{};GetWindowRect(editor,&afterLayout);
  Check(state.clientIcon==icon && EqualRect(&original,&afterLayout),"Repeated layout and toolbar updates reuse the icon without moving the document");
  for(int id:{IDC_NOTES_LIBRARY,IDC_NOTES_TRASH,IDC_NOTES_FIND_TOGGLE,IDC_NOTES_SNIPPETS,IDC_NOTES_TEMPLATES,IDC_NOTES_WORKLOG,IDC_NOTES_FORMAT}) {
    HWND control=GetDlgItem(owner,id);HDC screen=GetDC(control),dc=CreateCompatibleDC(screen);RECT area{};GetClientRect(control,&area);
    HBITMAP bitmap=CreateCompatibleBitmap(screen,area.right,area.bottom);HGDIOBJ old=SelectObject(dc,bitmap);
    const HGDIOBJ font=SelectObject(dc,GetOwnerDrawFont(control));SIZE text{};const auto label=NotesWindowText(control);
    GetTextExtentPoint32W(dc,label.c_str(),static_cast<int>(label.size()),&text);SelectObject(dc,font);
    Check(text.cx+ScaleByDpi(25,GetDpiForWindow(owner))<=area.right,"Every compact action has room for its icon and complete accessible label");
    for(UINT appearance:{0u,static_cast<UINT>(ODS_HOTLIGHT|ODS_FOCUS),static_cast<UINT>(ODS_DISABLED)}) {
      DRAWITEMSTRUCT draw{ODT_BUTTON,static_cast<UINT>(id),0,ODA_DRAWENTIRE,appearance,control,dc,area,0};
      Check(DrawNotesToolbarButton(draw,state),"The actual Notes renderer handles action icons in normal, hover and disabled states");
    }
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(control,screen);
  }
  std::string after;StreamClientNotesOut(editor,after);CHARRANGE selected{};NotesSelection(editor,selected);
  Check(before==after && selected.cpMin==4 && selected.cpMax==11 && Edit(owner)==editor,
      "Painting the refreshed controls preserves the note, selection and editor instance");
  RECT clientPicker{};GetWindowRect(GetDlgItem(owner,IDC_NOTES_SWITCH_CLIENT),&clientPicker);MapWindowPoints(nullptr,owner,reinterpret_cast<POINT *>(&clientPicker),2);
  Check(!GetDlgItem(owner,IDC_NOTES_CLIENT_HEADING) && clientPicker.left==ScaleByDpi(24,GetDpiForWindow(owner)),
      "The compact header aligns its client picker with the tabs without a redundant Notes label");
  const auto savedColors=g_themeColors;const bool savedDark=g_bThemeIsDark;LoadBakedThemesFromHeader();
  for(const wchar_t *name:{L"Dark - Gothic",L"Marine"}) {
    const auto theme=std::find_if(g_aCustomThemes.begin(),g_aCustomThemes.end(),[&](const ThemeEntry &entry){return entry.name==name;});
    Check(theme!=g_aCustomThemes.end(),"Style regression uses an existing dark or light theme");
    g_themeColors=theme->colors;g_bThemeIsDark=theme->dark;UpdateThemeBrushes();RefreshNotesForTheme();
    for(int id:{IDC_NOTES_LIBRARY,IDC_NOTES_TRASH,IDC_NOTES_FIND_TOGGLE,IDC_NOTES_SNIPPETS,IDC_NOTES_TEMPLATES,IDC_NOTES_WORKLOG,IDC_NOTES_FORMAT}) {
      HWND button=GetDlgItem(owner,id);RECT bounds{};GetClientRect(button,&bounds);
      HDC dc=GetDC(button);const HGDIOBJ font=SelectObject(dc,GetOwnerDrawFont(button));SIZE extent{};const auto text=NotesWindowText(button);
      GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&extent);SelectObject(dc,font);ReleaseDC(button,dc);
      Check(reinterpret_cast<HFONT>(SendMessageW(button,WM_GETFONT,0,0))==state.uiFont &&
          extent.cx+ScaleByDpi(25,GetDpiForWindow(owner))<=bounds.right,
          "Changing theme keeps the DPI-scaled UI font and complete action labels beside their icons");
    }
    RECT body{};GetWindowRect(editor,&body);NotesSelection(editor,selected);
    Check(EqualRect(&original,&body) && Edit(owner)==editor && selected.cpMin==4 && selected.cpMax==11,
        "Dark and light styling preserve editor size, the window layout and selected text");
  }
  g_themeColors=savedColors;g_bThemeIsDark=savedDark;UpdateThemeBrushes();RefreshNotesForTheme();
  fs::copy_file(fs::current_path()/L"icons/InfinityRND.ico",iconPath,fs::copy_options::overwrite_existing);
  ClearClientIconCache(state.clientName);PumpPanelMessages();
  Check(state.clientIcon && state.clientIcon!=icon && state.clientIconName==state.clientName,
      "Changing a client icon refreshes Notes through the existing icon cache notification");
  Check(SwitchClientNotes(owner,state,L"Style fallback QA",g_sDataDir/L"Sites/Style fallback QA") && state.clientIcon &&
      state.clientIconName==L"Style fallback QA" && !GetClientIconForUi(state.clientName,ScaleByDpi(18,GetDpiForWindow(owner))),
      "Client switching replaces the header icon and uses the launcher fallback for a client without a custom icon");
  Check(g_hNotesWindow==owner,"Client identity styling preserves the same Notes window");DestroyWindow(owner);
  ClearClientIconCache(L"Style icon QA");ClearClientIconCache(L"Style fallback QA");
}

static void NotesLinkBrowserDetails() {
  Fixture(L"Browser details QA");HWND owner=Open(L"Browser details QA");Command(owner,IDC_NOTES_ADD_TAB);
  Type(owner,L"Keep this note and its selected text.");Save(owner);Select(Edit(owner),5,14);
  auto &state=State(owner);
  std::string before;StreamClientNotesOut(Edit(owner),before);CHARRANGE selection{};NotesSelection(Edit(owner),selection);
  const auto edge=g_sEdgePath,chrome=g_sChromePath,brave=g_sBravePath,firefox=g_sFirefoxPath;
  g_sEdgePath=g_sDataDir/L"Details browsers/Edge/msedge.exe";g_sChromePath=g_sDataDir/L"Details browsers/Chrome/chrome.exe";
  g_sBravePath.clear();g_sFirefoxPath.clear();Put(g_sEdgePath,"Fixture executable - never run");Put(g_sChromePath,"Fixture executable - never run");
  Check(NotesSaveLinkBrowser(1),"Browser Details fixture starts with the Windows default choice");
  const auto opens=shellOpens;
  Command(owner,IDC_NOTES_DETAILS);HWND panel=state.panel,combo=GetDlgItem(panel,IDC_NOTES_LINK_BROWSER_PICK);
  // Opening Details flushes the cursor position first; compare browser changes
  // with that saved baseline rather than the earlier pre-selection notebook.
  const auto notebook=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes;
  Check(IsWindowVisible(combo) && (GetWindowLongPtrW(combo,GWL_STYLE)&WS_TABSTOP) && NotesWindowText(combo)==L"System default",
      "Details exposes the current browser as a labeled keyboard-accessible dropdown");
  Check(SendMessageW(combo,CB_GETCOUNT,0,0)==3,"Browser dropdown offers System default and only installed browsers");
  RECT url{},picker{},hint{},area{};GetWindowRect(GetDlgItem(panel,IDC_NOTES_INPUT2),&url);GetWindowRect(combo,&picker);
  GetWindowRect(GetDlgItem(panel,IDC_NOTES_DETAILS_HINT),&hint);GetWindowRect(panel,&area);
  Check(url.bottom<picker.top && picker.bottom<hint.top && hint.bottom<area.bottom && picker.left==url.left && picker.right==url.right,
      "Browser choice sits directly below the URL, with aligned fields and an unclipped explanation");
  Check(NotesWindowText(GetDlgItem(panel,IDC_NOTES_DETAILS_HINT)).find(L"outside client profiles")!=std::wstring::npos,
      "Details explains the regular PC session rather than a client browser profile");
  const auto choose=[&](UINT choice) {
    combo=GetDlgItem(panel,IDC_NOTES_LINK_BROWSER_PICK);
    for(LRESULT row=0;row<SendMessageW(combo,CB_GETCOUNT,0,0);++row) if(SendMessageW(combo,CB_GETITEMDATA,row,0)==choice) {
      SendMessageW(combo,CB_SETCURSEL,row,0);SendMessageW(panel,WM_COMMAND,MAKEWPARAM(IDC_NOTES_LINK_BROWSER_PICK,CBN_SELENDOK),reinterpret_cast<LPARAM>(combo));return;
    }
    throw std::runtime_error("Browser choice missing from Details");
  };
  SetDlgItemTextW(panel,IDC_NOTES_INPUT2,L"unfinished URL");SendMessageW(panel,WM_TIMER,1,0);
  Check(state.detailsDrafts.size()==1,"An unfinished URL is retained before choosing a browser");
  choose(2);
  Check(NotesLinkBrowser()==BrowserKind::Edge && NotesWindowText(combo)==L"Microsoft Edge" && state.panel==panel && IsWindow(panel),
      "Selecting Edge immediately remembers it and leaves Details open");
  Check(NotesWindowText(GetDlgItem(panel,IDC_NOTES_INPUT2))==L"unfinished URL" && state.detailsDrafts.size()==1 &&
      client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==notebook,
      "Global browser choice neither discards invalid URL drafts nor writes them to the notebook");
  std::string after;StreamClientNotesOut(Edit(owner),after);CHARRANGE current{};NotesSelection(Edit(owner),current);
  Check(before==after && selection.cpMin==current.cpMin && selection.cpMax==current.cpMax,
      "Changing browser preserves the note formatting and selected text");
  SetDlgItemTextW(panel,IDC_NOTES_INPUT2,L"https://tickets.example.test/T20260921.0622");SendMessageW(panel,WM_TIMER,1,0);
  Check(shellOpens==opens,"Choosing a browser itself never launches it");
  CloseNotesPanel(state);answers.push_back(IDYES);Command(owner,IDC_NOTES_OPEN_TICKET);
  Check(shellTarget==g_sEdgePath && shellParameters==QuoteCommandLineArgument(state.pages[state.activePage].ticketUrl),
      "The browser selected in Details handles ticket links using URL-only arguments");
  Command(owner,IDC_NOTES_DETAILS);panel=state.panel;combo=GetDlgItem(panel,IDC_NOTES_LINK_BROWSER_PICK);
  Check(NotesWindowText(combo)==L"Microsoft Edge","Reopening Details shows the remembered browser");
  SetFocus(combo);SendMessageW(combo,CB_SHOWDROPDOWN,TRUE,0);MSG accept{combo,WM_KEYDOWN,VK_RETURN};
  if(!ProcessNotesPanelMessage(accept)) {TranslateMessage(&accept);DispatchMessageW(&accept);}
  Check(IsWindow(panel) && state.panel==panel,"Enter accepts the browser dropdown without dismissing Details");
  SendMessageW(combo,CB_SHOWDROPDOWN,TRUE,0);SendMessageW(combo,WM_KEYDOWN,VK_DOWN,0);
  Check(NotesLinkBrowser()==BrowserKind::Edge,"Browsing the open dropdown does not commit a temporary keyboard selection");
  MSG cancel{combo,WM_KEYDOWN,VK_ESCAPE};
  if(!ProcessNotesPanelMessage(cancel)) {TranslateMessage(&cancel);DispatchMessageW(&cancel);}
  Check(IsWindow(panel) && NotesLinkBrowser()==BrowserKind::Edge && NotesWindowText(combo)==L"Microsoft Edge",
      "Escape cancels a browser selection while keeping Details and the previous preference");
  SendMessageW(combo,WM_KEYDOWN,VK_DOWN,0);
  Check(NotesLinkBrowser()==BrowserKind::Chrome && NotesWindowText(combo)==L"Google Chrome","Closed-dropdown arrow keys save the browser choice normally");
  choose(3);Check(NotesLinkBrowser()==BrowserKind::Chrome,"Available choices retain their browser identity when other browsers are absent");
  CloseNotesPanel(state);popupAction=2;Command(owner,IDC_NOTES_LINK_BROWSER);Command(owner,IDC_NOTES_DETAILS);panel=state.panel;
  Check(NotesWindowText(GetDlgItem(panel,IDC_NOTES_LINK_BROWSER_PICK))==L"Microsoft Edge","More and Details edit the same remembered browser preference");
  CloseNotesPanel(state);g_sEdgePath.clear();Command(owner,IDC_NOTES_DETAILS);panel=state.panel;combo=GetDlgItem(panel,IDC_NOTES_LINK_BROWSER_PICK);
  Check(NotesWindowText(combo)==L"Microsoft Edge (not installed)" && NotesLinkBrowser()==BrowserKind::Edge,
      "A missing chosen browser stays visible without silently changing the preference");
  choose(3);Check(NotesLinkBrowser()==BrowserKind::Chrome,"The visible setting lets users recover by choosing an installed browser");
  choose(1);Check(!NotesLinkBrowser() && NotesWindowText(combo)==L"System default","System default is directly available in Details");
  SendMessageW(panel,WM_ACTIVATE,WA_INACTIVE,reinterpret_cast<LPARAM>(owner));PumpPanelMessages();
  Check(!state.panel,"Clicking outside still closes Details after changing the browser");
  g_sEdgePath=edge;g_sChromePath=chrome;g_sBravePath=brave;g_sFirefoxPath=firefox;DestroyWindow(owner);
}

static void NotesLinkBrowserPreference() {
  Fixture(L"Browser preference QA");HWND owner=Open(L"Browser preference QA");
  const auto edge=g_sEdgePath,chrome=g_sChromePath,brave=g_sBravePath,firefox=g_sFirefoxPath;
  const auto browsers=g_sDataDir/L"Regular browser fixtures";
  g_sEdgePath=browsers/L"Edge/msedge.exe";g_sChromePath=browsers/L"Chrome/chrome.exe";
  g_sBravePath=browsers/L"Brave/brave.exe";g_sFirefoxPath=browsers/L"Firefox/firefox.exe";
  for(const auto &path:{g_sEdgePath,g_sChromePath,g_sBravePath,g_sFirefoxPath}) Put(path,"Fixture executable - never run");
  Check(std::any_of(std::begin(kNotesActions),std::end(kNotesActions),[](const auto &action){return action.id==IDC_NOTES_LINK_BROWSER && !action.needsNote;}),
      "One searchable Link browser preference is available even without an open note");
  popupAction=2;Command(owner,IDC_NOTES_LINK_BROWSER);
  Check(NotesLinkBrowser()==BrowserKind::Edge && NotesLinkBrowserLabel()==L"Microsoft Edge","Choosing Edge saves the preference for regular PC note links");
  popupAction=0;Command(owner,IDC_NOTES_LINK_BROWSER);
  Check(NotesLinkBrowser()==BrowserKind::Edge,"Dismissing the browser picker leaves the saved choice unchanged");
  NotesActionsState actions{false};HWND picker=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_ACTION_PANEL),owner,NotesActionsProc,reinterpret_cast<LPARAM>(&actions));
  SetDlgItemTextW(picker,IDC_NOTES_QUERY,L"Link browser");NotesActionsFill(picker,actions);
  wchar_t row[160]{};SendDlgItemMessageW(picker,IDC_NOTES_RESULTS,LB_GETTEXT,0,reinterpret_cast<LPARAM>(row));
  Check(actions.results.size()==1 && std::wstring(row)==L"Link browser: Microsoft Edge","Searchable More action shows the remembered browser choice without new editor controls");DestroyWindow(picker);
  Command(owner,IDC_NOTES_ADD_TAB);auto &state=State(owner);auto &page=state.pages[state.activePage];
  page.ticket=L"T20260921.0622";page.ticketUrl=L"https://tickets.example.test/T20260921.0622?view=work&next=regular";state.metadataChanged=true;
  const auto before=shellOpens;answers.push_back(IDNO);OpenNotesUrl(owner,state,page.ticketUrl,true);
  Check(shellOpens==before && lastNotice.find(L"Microsoft Edge")!=std::wstring::npos && lastNotice.find(L"outside client browser profiles")!=std::wstring::npos,
      "Link confirmation names the chosen browser and cancel opens nothing");
  answers.push_back(IDYES);OpenNotesUrl(owner,state,page.ticketUrl);
  Check(shellTarget==g_sEdgePath && shellHasParameters && shellParameters==QuoteCommandLineArgument(page.ticketUrl),
      "Edge receives only the web URL; no client profile directory or launcher flags");
  popupAction=3;Command(owner,IDC_NOTES_LINK_BROWSER);answers.push_back(IDYES);Command(owner,IDC_NOTES_OPEN_TICKET);
  Check(NotesLinkBrowser()==BrowserKind::Chrome && shellTarget==g_sChromePath && shellParameters==QuoteCommandLineArgument(page.ticketUrl),
      "The ticket header uses the same global browser preference as ordinary note links");
  NotesLibrary lib{owner,&state};HWND library=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&lib));
  for(size_t i=0;i<lib.results.size();++i) if(lib.results[i]==state.activePage) SendDlgItemMessageW(library,IDC_NOTES_RESULTS,LB_SETCURSEL,i,0);
  answers.push_back(IDYES);SendMessageW(library,WM_COMMAND,IDC_NOTES_OPEN_TICKET,0);
  Check(shellTarget==g_sChromePath && shellParameters==QuoteCommandLineArgument(page.ticketUrl),"All notes ticket links honor the same regular-browser choice");DestroyWindow(library);
  popupAction=4;Command(owner,IDC_NOTES_LINK_BROWSER);answers.push_back(IDYES);OpenNotesUrl(owner,state,page.ticketUrl);
  Check(NotesLinkBrowser()==BrowserKind::Brave && shellTarget==g_sBravePath,"Existing Brave support also uses its regular session for Notes links");
  popupAction=5;Command(owner,IDC_NOTES_LINK_BROWSER);answers.push_back(IDYES);OpenNotesUrl(owner,state,page.ticketUrl);
  Check(NotesLinkBrowser()==BrowserKind::Firefox && shellTarget==g_sFirefoxPath && shellParameters==QuoteCommandLineArgument(page.ticketUrl),
      "Firefox receives only the URL, without a client profile or new-instance argument");
  popupAction=2;Command(owner,IDC_NOTES_LINK_BROWSER);const auto edgeFixture=g_sEdgePath;g_sEdgePath.clear();
  const auto unavailable=shellOpens;OpenNotesUrl(owner,state,page.ticketUrl);
  Check(shellOpens==unavailable && lastNotice.find(L"not available")!=std::wstring::npos && NotesLinkBrowser()==BrowserKind::Edge,
      "An unavailable chosen browser is reported and never silently falls back to the default browser");g_sEdgePath=edgeFixture;
  shellResult=31;answers.push_back(IDYES);OpenNotesUrl(owner,state,page.ticketUrl);shellResult=33;
  Check(lastNotice.find(L"More > Link browser")!=std::wstring::npos && NotesLinkBrowser()==BrowserKind::Edge,"Launch failure explains the setting and retains the chosen browser");
  Check(SwitchClientNotes(owner,state,L"Destination QA",g_sDataDir/L"Sites/Destination QA") && NotesLinkBrowser()==BrowserKind::Edge,
      "The browser preference survives switching client notebooks in the same Notes window");
  DestroyWindow(owner);owner=Open(L"Browser preference QA");
  Check(NotesLinkBrowser()==BrowserKind::Edge,"The regular-browser preference persists across Notes window reopening");
  popupAction=1;Command(owner,IDC_NOTES_LINK_BROWSER);answers.push_back(IDYES);OpenNotesUrl(owner,State(owner),L"https://example.test/default");
  Check(!NotesLinkBrowser() && shellTarget==L"https://example.test/default" && !shellHasParameters,"System default restores Windows URL association without changing default browser settings");
  const auto invalid=shellOpens;OpenNotesUrl(owner,State(owner),L"file:///unsafe");
  Check(shellOpens==invalid,"Browser preference never bypasses validation of web URLs");
  g_sEdgePath=edge;g_sChromePath=chrome;g_sBravePath=brave;g_sFirefoxPath=firefox;DestroyWindow(owner);
}

static void NotesTicketHeaderSizing() {
  Fixture(L"Ticket sizing QA");HWND owner=Open(L"Ticket sizing QA");Command(owner,IDC_NOTES_ADD_TAB);
  auto &state=State(owner);auto &page=state.pages[state.activePage];Type(owner,L"Ticket header sizing preserves this note.");
  const HWND editor=Edit(owner);std::string before;StreamClientNotesOut(editor,before);
  for(UINT dpi:{96u,144u,192u}) for(int width:{900,1240}) for(const auto &ticket:std::vector<std::wstring>{L"1048",L"T20260921.0622",std::wstring(128,L'W')}) {
    page.ticket=ticket;page.tags={L"printing",L"rollout"};UpdateNotesToolbar(owner,state);
    SetWindowPos(owner,nullptr,0,0,ScaleByDpi(width,dpi),ScaleByDpi(600,dpi),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);LayoutClientNotes(owner,dpi);
    RECT reference{},tags{},area{};
    GetWindowRect(GetDlgItem(owner,IDC_NOTES_OPEN_TICKET),&reference);GetWindowRect(GetDlgItem(owner,IDC_NOTES_CONTEXT),&tags);GetWindowRect(owner,&area);
    Check(reference.left>=area.left && reference.right<tags.left && tags.right<=area.right &&
        !GetDlgItem(owner,IDC_NOTES_NOTE_HEADING) && (GetWindowLongPtrW(GetDlgItem(owner,IDC_NOTES_TICKET_REFERENCE),GWL_STYLE)&WS_VISIBLE),
        "Full ticket references and tags fit on their compact line without a duplicated note title across DPI");
    Check(NotesWindowText(GetDlgItem(owner,IDC_NOTES_OPEN_TICKET))==L"Ticket "+ticket && reference.right-reference.left<=ScaleByDpi(360,dpi),
        "Ticket header keeps the full value with a sensible maximum display width");
    if(ticket.size()<128) {
      HDC dc=GetDC(GetDlgItem(owner,IDC_NOTES_OPEN_TICKET));HGDIOBJ font=SelectObject(dc,state.uiFont);
      SIZE extent{};const std::wstring label=L"Ticket "+ticket;GetTextExtentPoint32W(dc,label.c_str(),static_cast<int>(label.size()),&extent);
      SelectObject(dc,font);ReleaseDC(GetDlgItem(owner,IDC_NOTES_OPEN_TICKET),dc);
      Check(extent.cx<=reference.right-reference.left-ScaleByDpi(16,dpi),"Complete T20260921.0622-style IDs fit without truncation, including at minimum width");
    }
    page.tags.assign(16,std::wstring(32,L'W'));UpdateNotesToolbar(owner,state);LayoutClientNotes(owner,dpi);
    GetWindowRect(GetDlgItem(owner,IDC_NOTES_OPEN_TICKET),&reference);GetWindowRect(GetDlgItem(owner,IDC_NOTES_CONTEXT),&tags);
    Check(reference.right<tags.left && tags.right<=area.right,"Long tag lists yield width to the ticket instead of pushing it out of the header");
  }
  LayoutClientNotes(owner,GetDpiForWindow(owner));
  NMTTDISPINFOW tooltip{};tooltip.hdr.hwndFrom=state.toolbarTooltip;tooltip.hdr.idFrom=reinterpret_cast<UINT_PTR>(GetDlgItem(owner,IDC_NOTES_OPEN_TICKET));tooltip.hdr.code=TTN_GETDISPINFOW;
  SendMessageW(owner,WM_NOTIFY,tooltip.hdr.idFrom,reinterpret_cast<LPARAM>(&tooltip));
  Check(tooltip.lpszText && std::wstring(tooltip.lpszText).find(page.ticket)!=std::wstring::npos,"An unusually long ticket also exposes its full reference on hover");
  std::string after;StreamClientNotesOut(editor,after);Check(Edit(owner)==editor && before==after,"Autosizing the ticket header preserves the editor and its formatting");DestroyWindow(owner);
}

static void CaptureNotesLinkBrowserDetails(const fs::path &output) {
  Fixture(L"Northstar Office (Demo)");HWND owner=Open(L"Northstar Office (Demo)");Command(owner,IDC_NOTES_ADD_TAB);
  auto &state=State(owner);auto &page=state.pages[state.activePage];
  page.ticket=L"T20260921.0622";page.ticketUrl=L"https://tickets.example.test/T20260921.0622";page.status=2;page.tags={L"printing",L"rollout"};
  state.metadataChanged=true;Save(owner);
  const auto edge=g_sEdgePath,chrome=g_sChromePath;
  g_sEdgePath=g_sDataDir/L"Capture browsers/Edge/msedge.exe";g_sChromePath=g_sDataDir/L"Capture browsers/Chrome/chrome.exe";
  Put(g_sEdgePath,"Fixture executable - never run");Put(g_sChromePath,"Fixture executable - never run");NotesSaveLinkBrowser(2);
  fs::create_directories(output);LoadBakedThemesFromHeader();
  for(const auto &[name,file]:std::vector<std::pair<std::wstring,std::wstring>>{{L"Dark - Gothic",L"gothic-details.png"},{L"Marine",L"marine-details.png"}}) {
    const auto theme=std::find_if(g_aCustomThemes.begin(),g_aCustomThemes.end(),[&](const ThemeEntry &entry){return entry.name==name;});
    Check(theme!=g_aCustomThemes.end(),"Browser settings visual review uses an actual built-in theme");
    g_themeColors=theme->colors;g_bThemeIsDark=theme->dark;UpdateThemeBrushes();RefreshNotesForTheme();
    Command(owner,IDC_NOTES_DETAILS);HWND panel=state.panel;ShowWindow(panel,SW_SHOW);
    RedrawWindow(panel,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    RECT area{};GetWindowRect(panel,&area);HDC screen=GetDC(panel),dc=CreateCompatibleDC(screen);
    HBITMAP bitmap=CreateCompatibleBitmap(screen,area.right-area.left,area.bottom-area.top);HGDIOBJ prior=SelectObject(dc,bitmap);
    Check(PrintWindow(panel,dc,2)!=FALSE,"Capture the actual Details panel with the visible browser choice");SelectObject(dc,prior);
    const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
    {Gdiplus::Bitmap snapshot(bitmap,nullptr);Check(snapshot.Save((output/file).c_str(),&png,nullptr)==Gdiplus::Ok,"Save native browser settings visual evidence");}
    DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(panel,screen);CloseNotesPanel(state);
  }
  g_sEdgePath=edge;g_sChromePath=chrome;DestroyWindow(owner);
}

static void CaptureCompactNotesActionRow(const fs::path &output) {
  Fixture(L"Northstar Office (Demo)");
  fs::copy_file(fs::current_path()/L"icons/InfinityRND.ico",g_sDataDir/L"Sites/Northstar Office (Demo)/client.ico",fs::copy_options::overwrite_existing);
  HWND owner=Open(L"Northstar Office (Demo)");Command(owner,IDC_NOTES_ADD_TAB);
  auto &state=State(owner);auto &page=state.pages[state.activePage];
  page.name=L"Printer rollout";page.ticket=L"T20260921.0622";page.status=2;page.tags={L"printing",L"rollout"};
  page.ticketUrl=L"https://tickets.example.test/T20260921.0622";state.metadataChanged=true;
  RefreshNotesTabs(owner,state);
  Type(owner,L"Office printer rollout\r\nTicket T20260921.0622 | Print server: PRINT01 | Owner: Alex Morgan\r\n\r\nTwo new office printers are ready for installation. Existing queues remain in service while the vendor confirms the rollout window.\r\n\r\nNext action\r\nConfirm the installation window with the vendor, then complete a test print from an office PC.\r\n\r\nHandover\r\nKeep the existing queues until both printers pass testing. Record the model, driver and port before closing the ticket.\r\n\r\nDemonstration note - fictional client and ticket.");
  Select(Edit(owner),0,21);NotesCharacterFormat(owner,state,CFM_BOLD|CFM_SIZE,CFE_BOLD,0,480);Select(Edit(owner),0,0);Save(owner);
  fs::create_directories(output);
  LoadBakedThemesFromHeader();
  for (const auto &[name,width,file,hover] : std::vector<std::tuple<std::wstring,int,std::wstring,bool>>{
      {L"Dark - Gothic",900,L"gothic-writing.png",false},{L"Dark - Gothic",1120,L"gothic-selection.png",true},
      {L"Marine",900,L"marine-selection.png",true},
      {L"Dark - Gothic",900,L"gothic-unlinked.png",false}}) {
    if (file==L"gothic-unlinked.png") {
      page.ticket.clear();page.ticketUrl.clear();page.tags.clear();page.status=0;page.name=L"Untitled 4";
      Select(Edit(owner),0,-1);Type(owner,L"");
      NotesCharacterFormat(owner,state,CFM_BOLD|CFM_SIZE,0,0,240);RefreshNotesTabs(owner,state);
    }
    const auto theme=std::find_if(g_aCustomThemes.begin(),g_aCustomThemes.end(),[&](const ThemeEntry &entry){return entry.name==name;});
    Check(theme!=g_aCustomThemes.end(),"Native visual review loads a real built-in theme");
    g_themeColors=theme->colors;g_bThemeIsDark=theme->dark;UpdateThemeBrushes();RefreshNotesForTheme();
    const UINT dpi=GetDpiForWindow(owner);SetWindowPos(owner,HWND_TOP,80,80,ScaleByDpi(width,dpi),ScaleByDpi(650,dpi),SWP_SHOWWINDOW);
    g_hHotButton=nullptr;
    UpdateNotesToolbar(owner,state);LayoutClientNotes(owner,dpi);
    if (hover) {
      FINDTEXTEXW phrase{{0,-1},L"office printers",{}};
      Check(SendMessageW(Edit(owner),EM_FINDTEXTEXW,FR_DOWN,reinterpret_cast<LPARAM>(&phrase))>=0,"Native capture selects the exact demo phrase");
      Select(Edit(owner),phrase.chrgText.cpMin,phrase.chrgText.cpMax);UpdateNotesToolbar(owner,state);ShowNotesFormat(owner,state,true);
    } else {HideNotesFormat(owner,state);Select(Edit(owner),0,0);}
    RedrawWindow(owner,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    RECT area{};GetWindowRect(owner,&area);HDC screen=GetDC(owner),dc=CreateCompatibleDC(screen);
    HBITMAP bitmap=CreateCompatibleBitmap(screen,area.right-area.left,area.bottom-area.top);HGDIOBJ prior=SelectObject(dc,bitmap);
    Check(PrintWindow(owner,dc,2)!=FALSE,"Capture the actual native Notes dialog and its child controls");
    SelectObject(dc,prior);
    const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
    {Gdiplus::Bitmap snapshot(bitmap,nullptr);Check(snapshot.Save((output/file).c_str(),&png,nullptr)==Gdiplus::Ok,"Save the native Notes visual review artifact");}
    DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(owner,screen);
  }
  g_hHotButton=nullptr;DestroyWindow(owner);
}

static void NotesAuditFailureRegressions() {
  Fixture(L"Failure regression QA");const auto root=g_sDataDir/L"Sites/Failure regression QA";
  client_notes::NotePage source{L"Source note",true,NotesPlainRtf(L"SOURCE NOTE CONTENT"),{}};
  source.id=1;source.open=source.active=true;
  client_notes::NotePage target{L"Target note",true,NotesPlainRtf(L"IMPORTANT TARGET CONTENT"),{}};
  target.id=2;target.open=false;
  Check(client_notes::WriteNotebook(root,{source,target},client_notes::ReadNotebook(root)),"Failure fixture persists distinct documents");
  HWND owner=Open(L"Failure regression QA");auto &state=State(owner);const HWND sourceEdit=Edit(owner);
  const auto correct=[&] {
    const auto disk=client_notes::ReadNotebook(root);
    return disk.pages.size()==2 && NotesPlain(disk.pages[0])==L"SOURCE NOTE CONTENT" &&
        NotesPlain(disk.pages[1])==L"IMPORTANT TARGET CONTENT";
  };
  Select(sourceEdit,2,7);CHARRANGE before{};NotesSelection(sourceEdit,before);
  failNextNotesEditor=true;
  Check(!NotesOpenSavedPage(owner,state,1) && !failNextNotesEditor,"All notes reports editor allocation failure instead of success");
  CHARRANGE after{};NotesSelection(Edit(owner),after);
  Check(state.activePage==0 && state.attachedPageId==1 && Edit(owner)==sourceEdit && !state.pages[1].open &&
      state.pages[0].id==1 && state.pages[1].id==2 && before.cpMin==after.cpMin && before.cpMax==after.cpMax && correct(),
      "Failed reopen rolls back tab order, open flag, identity, HWND, selection and saved content");
  failNextNotesEditor=true;Command(owner,IDC_NOTES_ADD_TAB);
  Check(!failNextNotesEditor && state.pages.size()==2 && state.baselines.size()==2 && Edit(owner)==sourceEdit && correct(),
      "Failed new tab leaves no orphan or wrong-note document");
  failNextNotesEditor=true;
  Check(!NotesAddPage(owner,state,{L"Failed template",true,NotesPlainRtf(L"TEMPLATE CONTENT"),{}}) &&
      state.pages.size()==2 && state.activePage==0 && correct(),"Failed template insertion rolls back safely");
  state.pages[1].open=true;state.metadataChanged=true;RefreshNotesTabs(owner,state);Save(owner);
  HWND tabs=GetDlgItem(owner,IDC_NOTES_TABS);TabCtrl_SetCurSel(tabs,1);
  NMHDR changed{tabs,IDC_NOTES_TABS,TCN_SELCHANGE};failNextNotesEditor=true;
  SendMessageW(owner,WM_NOTIFY,IDC_NOTES_TABS,reinterpret_cast<LPARAM>(&changed));
  Check(!failNextNotesEditor && state.activePage==0 && TabCtrl_GetCurSel(tabs)==0 && Edit(owner)==sourceEdit && correct(),
      "Native tab selection failure restores previous selection and document");
  failNextNotesEditor=true;SendMessageW(tabs,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);
  Check(!failNextNotesEditor && state.activePage==0 && TabCtrl_GetCurSel(tabs)==0 && correct(),"Mouse-wheel allocation failure cannot switch note identity");
  RECT tab{};TabCtrl_GetItemRect(tabs,1,&tab);failNextNotesEditor=true;
  SendMessageW(tabs,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(tab.right-ScaleByDpi(10,GetDpiForWindow(tabs)),tab.top+10));
  Check(!failNextNotesEditor && state.pages[0].open && state.pages[1].open && state.activePage==0 && correct(),
      "Clicking an unloaded tab's X cannot close the previous note after allocation failure");
  // A defensive capture refuses even a caller that supplies a mismatched index.
  state.activePage=1;
  Check(!CaptureNotesPage(owner,state,false) && correct(),"Capture rejects a mismatched attached document identity");
  state.activePage=0;
  Fixture(L"Switch failure QA");auto other=target;other.id=1;other.open=other.active=true;
  const auto otherRoot=g_sDataDir/L"Sites/Switch failure QA";
  Check(client_notes::WriteNotebook(otherRoot,{other},client_notes::ReadNotebook(otherRoot)),"Switch-failure fixture uses a colliding local note identity");
  failNextNotesEditor=true;
  Check(!SwitchClientNotes(owner,state,L"Switch failure QA",otherRoot) && !failNextNotesEditor &&
      state.clientName==L"Failure regression QA" && state.attachedPageId==1 && Edit(owner)==sourceEdit &&
      (GetWindowLongPtrW(sourceEdit,GWL_STYLE)&WS_VISIBLE) && correct(),"Failed client switch restores the old notebook and visible HWND despite colliding note IDs");
  failNextNotesEditor=true;Command(owner,IDC_NOTES_CLOSE_TAB);
  Check(!failNextNotesEditor && !state.pages[0].open && state.pages[1].open && !HasActiveNote(state) &&
      !state.attachedEditor && correct(),"Failed adjacent-note load after closing safely shows no document without overwriting either note");
  Check(NotesOpenSavedPage(owner,state,1) && NotesWindowText(Edit(owner))==L"IMPORTANT TARGET CONTENT", "Retry after adjacent load failure opens the correct saved document");
  DestroyWindow(owner);
}

static void NotesAuditLibraryAndLinks() {
  Fixture(L"Bulk regression QA");HWND owner=Open(L"Bulk regression QA");
  Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Current actionable note");Save(owner);
  auto &state=State(owner);const uint64_t current=state.pages[state.activePage].id;
  Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Archived actionable note");Save(owner);
  const uint64_t archived=state.pages[state.activePage].id;Command(owner,IDC_NOTES_ARCHIVE);
  NotesLibrary library{owner,&state};HWND window=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&library));
  SendDlgItemMessageW(window,IDC_NOTES_FILTER,CB_SETCURSEL,4,0);library.selecting=true;library.selected={current,archived};NotesLibraryFill(window,library);
  for (const auto id:{current,archived}) {
    const auto row=std::find_if(library.results.begin(),library.results.end(),[&](size_t i){return state.pages[i].id==id;});
    Check(row!=library.results.end(),"Mixed-selection fixture finds both notes");
    SendDlgItemMessageW(window,IDC_NOTES_RESULTS,LB_SETCURSEL,row-library.results.begin(),0);NotesLibraryPreview(window,library);
    Check(IsWindowEnabled(GetDlgItem(window,IDC_NOTES_ARCHIVE)) && IsWindowEnabled(GetDlgItem(window,IDC_NOTES_RESTORE)),
        "Mixed-selection Archive and Restore follow selected notes independent of preview focus");
  }
  SendMessageW(window,WM_COMMAND,IDC_NOTES_RESTORE,0);
  const auto currentPage=std::find_if(state.pages.begin(),state.pages.end(),[&](const auto &p){return p.id==current;});
  const auto archivedPage=std::find_if(state.pages.begin(),state.pages.end(),[&](const auto &p){return p.id==archived;});
  Check(currentPage->open && !archivedPage->archived && !archivedPage->open,"Mixed-selection Restore leaves current open notes unchanged");
  DestroyWindow(window);DestroyWindow(owner);

  Fixture(L"Link regression QA");owner=Open(L"Link regression QA");Command(owner,IDC_NOTES_ADD_TAB);
  Type(owner,L"Support");Select(Edit(owner),0,7);inputs.push_back({L"Support",L"https://example.test/deleted-link"});Command(owner,IDC_NOTES_LINK);Save(owner);
  auto &linkState=State(owner);auto &page=linkState.pages[linkState.activePage];
  Check(NotesHandoverHtml(linkState,{linkState.activePage}).find(L"https://example.test/deleted-link")!=std::wstring::npos,
      "Handover preserves a real labeled document link");
  Select(Edit(owner),0,7);Command(owner,IDC_NOTES_LINK_REMOVE);Save(owner);
  const auto html=NotesHandoverHtml(linkState,{linkState.activePage});
  Check(page.links.empty() && html.find(L"https://example.test/deleted-link")==std::wstring::npos &&
      client_notes::ReadNotebook(linkState.clientRoot).pages[linkState.activePage].links.empty(),"Removing a link reconciles its saved mapping and handover output");
  SendMessageW(Edit(owner),EM_UNDO,0,0);
  const auto opens=shellOpens;answers.push_back(IDYES);
  ENLINK clicked{{Edit(owner),IDC_CLIENT_NOTES_TEXT,EN_LINK},WM_LBUTTONUP,0,0,{0,7}};
  SendMessageW(owner,WM_NOTIFY,IDC_CLIENT_NOTES_TEXT,reinterpret_cast<LPARAM>(&clicked));
  Check(shellOpens==opens+1 && (shellTarget==L"https://example.test/deleted-link" || shellParameters==QuoteCommandLineArgument(L"https://example.test/deleted-link")),
      "Clicking an undone labeled link before autosave resolves its restored address immediately");
  Save(owner);
  Check(page.links.size()==1 && NotesHandoverHtml(linkState,{linkState.activePage}).find(L"https://example.test/deleted-link")!=std::wstring::npos,
      "Undoing removal restores the live link and its address mapping");
  SendMessageW(Edit(owner),EM_REDO,0,0);Save(owner);
  Check(page.links.empty(),"Redoing link removal again removes its saved address mapping");
  page.links.emplace_back(L"Support",L"https://example.test/stale-link");
  Check(NotesHandoverHtml(linkState,{linkState.activePage}).find(L"https://example.test/stale-link")==std::wstring::npos,
      "Handover ignores stale mappings in unopened or legacy notes");
  Save(owner);
  Select(Edit(owner),0,-1);Type(owner,L"Support Support");Select(Edit(owner),0,7);
  inputs.push_back({L"Support",L"https://example.test/shared-link"});Command(owner,IDC_NOTES_LINK);Save(owner);
  Select(Edit(owner),8,15);inputs.push_back({L"Support",L"https://example.test/shared-link"});Command(owner,IDC_NOTES_LINK);Save(owner);
  Check(page.links.size()==1 && NotesRangeLinkEffects(Edit(owner),{0,7}) && NotesRangeLinkEffects(Edit(owner),{8,15}),
      "Repeated friendly labels share an address while remaining real native link ranges");
  Select(Edit(owner),0,7);Command(owner,IDC_NOTES_LINK_REMOVE);Save(owner);
  Check(page.links.size()==1 && !NotesRangeLinkEffects(Edit(owner),{0,7}) && NotesRangeLinkEffects(Edit(owner),{8,15}),
      "Removing one repeated label retains the remaining live mapping");
  const uint64_t linkId=page.id;Command(owner,IDC_NOTES_CLOSE_TAB);
  const auto linkIndex=static_cast<size_t>(std::find_if(linkState.pages.begin(),linkState.pages.end(),[&](const auto &p){return p.id==linkId;})-linkState.pages.begin());
  Check(NotesOpenSavedPage(owner,linkState,linkIndex) && NotesWindowText(Edit(owner))==L"Support Support" &&
      !NotesRangeLinkEffects(Edit(owner),{0,7}) && NotesRangeLinkEffects(Edit(owner),{8,15}),
      "Saved friendly-link round trip restores only the still-linked repeated label");
  Check(NotesHandoverHtml(linkState,{linkState.activePage}).find(L"https://example.test/shared-link")!=std::wstring::npos,
      "Saved repeated link remains in the handover after closing and reopening");
  DestroyWindow(owner);

  Fixture(L"Deleted draft regression QA");owner=Open(L"Deleted draft regression QA");Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Deleted note");Save(owner);
  auto &deletedState=State(owner);const uint64_t deletedId=deletedState.pages[deletedState.activePage].id;
  NotesShowPanel(owner,&deletedState,true);SetDlgItemTextW(deletedState.panel,IDC_NOTES_INPUT2,L"invalid URL");CloseNotesPanel(deletedState);
  Check(deletedState.detailsDrafts.contains(deletedId),"Invalid Details draft is retained while its note exists");
  Command(owner,IDC_NOTES_DELETE);NotesLibrary trash{owner,&deletedState};trash.initialFilter=3;
  window=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&trash));
  answers.push_back(IDYES);SendMessageW(window,WM_COMMAND,IDC_NOTES_DELETE,0);DestroyWindow(window);
  Check(!deletedState.detailsDrafts.contains(deletedId) && deletedState.detailsDrafts.empty(),"Permanent deletion also removes the note's invalid Details draft");
  lastNotice.clear();CloseClientNotesDialog(owner,deletedState);
  Check(!IsWindow(owner) && lastNotice.empty(),"Closing after permanent deletion produces no orphaned Details warning");
}

static void NotesAuditInactiveDetails() {
  Fixture(L"Details switch destination QA");
  for (const auto operation:{IDC_NOTES_CLOSE_TAB,IDC_NOTES_ARCHIVE,IDC_NOTES_DELETE,IDC_NOTES_CLOSE_ALL}) {
    const auto client=L"Inactive details QA "+std::to_wstring(operation);Fixture(client);HWND owner=Open(client);auto &state=State(owner);
    Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Keep this note's text");
    state.pages[state.activePage].name=L"Affected note A";state.metadataChanged=true;Save(owner);
    const auto id=state.pages[state.activePage].id;
    NotesShowPanel(owner,&state,true);SetDlgItemTextW(state.panel,IDC_NOTES_INPUT2,L"invalid draft URL");CloseNotesPanel(state);
    Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Unrelated note B");Save(owner);const auto otherId=state.pages[state.activePage].id;
    const auto index=[&](uint64_t note) {return static_cast<size_t>(std::find_if(state.pages.begin(),state.pages.end(),[&](const auto &p){return p.id==note;})-state.pages.begin());};
    NotesOpenSavedPage(owner,state,index(id));Command(owner,operation);
    if (operation!=IDC_NOTES_CLOSE_ALL) NotesOpenSavedPage(owner,state,index(otherId));
    Check(state.detailsDrafts.contains(id),"Closing, archiving or trashing retains the affected unfinished Details until an explicit decision");
    std::wstring error;Check(!FlushClientNotesForBackup({client},error) && error.find(L"Affected note A")!=std::wstring::npos,
        "Backup refusal identifies the inactive note whose Details need correction");
    answers.push_back(IDNO);CloseClientNotesDialog(owner,state);
    Check(IsWindow(owner) && lastNotice.find(L"Affected note A")!=std::wstring::npos,"Close warning names the affected note instead of the current unrelated note");
    if (operation==IDC_NOTES_ARCHIVE || operation==IDC_NOTES_DELETE) {
      Check(!state.panel && openedLibraryNote==id && openedLibraryFilter==(operation==IDC_NOTES_DELETE ? 3 : 2) &&
          !state.pages[index(id)].open && (operation==IDC_NOTES_DELETE ? state.pages[index(id)].deleted : state.pages[index(id)].archived),
          "Review opens the affected Archive or Trash view without restoring or opening that note");
      NotesLibrary library{owner,&state};library.initialFilter=openedLibraryFilter;library.initialNote=id;
      HWND window=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&library));
      const auto row=SendDlgItemMessageW(window,IDC_NOTES_RESULTS,LB_GETCURSEL,0,0);
      Check(row>=0 && state.pages[library.results[row]].id==id,"Archive and Trash correction views focus the identified affected note");DestroyWindow(window);
    } else {
      const auto *panel=reinterpret_cast<NotesPanel *>(GetWindowLongPtrW(state.panel,DWLP_USER));
      Check(panel && panel->note==id && state.attachedPageId==id && NotesWindowText(GetDlgItem(state.panel,IDC_NOTES_INPUT2))==L"invalid draft URL",
          "Review reopens the actual closed note and its invalid Details even with no active note");CloseNotesPanel(state);
    }
    if (operation!=IDC_NOTES_CLOSE_ALL) NotesOpenSavedPage(owner,state,index(otherId));
    answers.push_back(IDNO);
    Check(!SwitchClientNotes(owner,state,L"Details switch destination QA",g_sDataDir/L"Sites/Details switch destination QA") && state.clientName==client &&
        (operation==IDC_NOTES_ARCHIVE || operation==IDC_NOTES_DELETE ? openedLibraryNote==id : state.attachedPageId==id),
        "Declining client switch keeps ownership and directs review to the affected note");
    CloseNotesPanel(state);
    const auto text=NotesPlain(state.pages[index(id)]);answers.push_back(IDYES);CloseClientNotesDialog(owner,state);
    Check(!IsWindow(owner) && text==L"Keep this note's text", "Explicitly discarding identified invalid Details closes safely and preserves note text");
  }
}

static void NotesBulkLinkUndoBindings();
static void NotesLinkUndoBindings() {
  Fixture(L"Link Undo bindings QA");HWND owner=Open(L"Link Undo bindings QA");Command(owner,IDC_NOTES_ADD_TAB);
  auto &state=State(owner);Type(owner,L"Support");
  const auto link=[&](const std::wstring &label,const std::wstring &url,LONG first,LONG last) {
    Select(Edit(owner),first,last);inputs.push_back({label,url});Command(owner,IDC_NOTES_LINK);Save(owner);
  };
  const auto click=[&](const std::wstring &url,LONG first,LONG last) {
    if(NotesNativeLinkAddress(Edit(owner),State(owner),{first,last})!=url) {
      LONG cookie=0;const bool linked=NotesRangeLinkEffects(Edit(owner),{first,last},nullptr,&cookie);
      std::wcout<<L"Binding mismatch: expected "<<url<<L", cookie "<<cookie<<L", linked "<<linked<<L", actual "<<NotesNativeLinkAddress(Edit(owner),State(owner),{first,last})<<std::endl;
    }
    const auto before=shellOpens;answers.push_back(IDYES);ENLINK event{{Edit(owner),IDC_CLIENT_NOTES_TEXT,EN_LINK},WM_LBUTTONUP,0,0,{first,last}};
    SendMessageW(owner,WM_NOTIFY,IDC_CLIENT_NOTES_TEXT,reinterpret_cast<LPARAM>(&event));
    Check(shellOpens==before+1 && (shellTarget==url || shellParameters==QuoteCommandLineArgument(url)),"Native linked range opens its actual immutable target");
  };
  const auto mapped=[&](const std::wstring &label,const std::wstring &url) {
    Save(owner);const auto &page=state.pages[state.activePage];
    const auto match=[&](const auto &links){return std::find(links.begin(),links.end(),std::pair{label,url})!=links.end();};
    Check(match(page.links) && match(client_notes::ReadNotebook(state.clientRoot).pages[state.activePage].links) &&
        NotesHandoverHtml(state,{state.activePage}).find(url)!=std::wstring::npos,"Captured, persisted and exported link targets follow the native Undo state");
  };
  link(L"Support",L"https://example.test/target-A",0,7);const auto initial=state.pages[state.activePage].rtf;const auto noteId=state.attachedPageId;
  Check(GetWindowTextLengthW(Edit(owner))==7 && NotesWindowText(Edit(owner))==L"Support","Native cookie binding adds no hidden URL text or character offsets");
  link(L"Support",L"https://example.test/target-B",0,7);
  Check(state.pages[state.activePage].rtf==initial,"Editing a target preserves the label's document bytes");
  Command(owner,IDC_NOTES_UNDO);click(L"https://example.test/target-A",0,7);mapped(L"Support",L"https://example.test/target-A");
  Command(owner,IDC_NOTES_REDO);click(L"https://example.test/target-B",0,7);mapped(L"Support",L"https://example.test/target-B");
  link(L"Support",L"https://example.test/target-A",0,7);Select(Edit(owner),0,7);Command(owner,IDC_NOTES_LINK_REMOVE);Save(owner);
  link(L"Support",L"https://example.test/target-B",0,7);
  Command(owner,IDC_NOTES_UNDO);
  Check(!NotesRangeLinkEffects(Edit(owner),{0,7}),"Undoing reinsert restores the previously unlinked label in one action");
  Command(owner,IDC_NOTES_UNDO);click(L"https://example.test/target-A",0,7);mapped(L"Support",L"https://example.test/target-A");
  Command(owner,IDC_NOTES_REDO);Command(owner,IDC_NOTES_REDO);click(L"https://example.test/target-B",0,7);
  link(L"Portal",L"https://example.test/target-C",0,7);
  Command(owner,IDC_NOTES_UNDO);
  Check(NotesWindowText(Edit(owner))==L"Support","Renaming a link and editing its address are one native Undo action");click(L"https://example.test/target-B",0,7);
  Command(owner,IDC_NOTES_REDO);Check(NotesWindowText(Edit(owner))==L"Portal","Native Redo restores the link's new label");click(L"https://example.test/target-C",0,6);
  Select(Edit(owner),2,2);Type(owner,L"X");Save(owner);
  Check(NotesWindowText(Edit(owner))==L"PoXrtal" && NotesNativeLinkAddress(Edit(owner),state,{0,7})==L"https://example.test/target-C",
      "Typing within a friendly label retains its immutable URL binding");mapped(L"PoXrtal",L"https://example.test/target-C");
  Select(Edit(owner),1,4);Command(owner,IDC_NOTES_BOLD);Save(owner);mapped(L"PoXrtal",L"https://example.test/target-C");
  Select(Edit(owner),0,-1);Command(owner,IDC_NOTES_CLEAR);Type(owner,L"One / One");Save(owner);
  link(L"One",L"https://example.test/shared-D",0,3);link(L"One",L"https://example.test/shared-D",6,9);
  link(L"One",L"https://example.test/shared-E",0,3);Command(owner,IDC_NOTES_UNDO);
  click(L"https://example.test/shared-D",0,3);click(L"https://example.test/shared-D",6,9);mapped(L"One",L"https://example.test/shared-D");
  Select(Edit(owner),0,3);Command(owner,IDC_NOTES_LINK_REMOVE);Save(owner);
  Check(NotesNativeLinkAddress(Edit(owner),state,{0,3}).empty() && NotesNativeLinkAddress(Edit(owner),state,{6,9})==L"https://example.test/shared-D",
      "Removing one repeated label leaves the other occurrence's native target intact");
  const auto index=[&](uint64_t id) {return static_cast<size_t>(std::find_if(state.pages.begin(),state.pages.end(),[&](const auto &page){return page.id==id;})-state.pages.begin());};
  for(int i=0;i<20;++i) Check(NotesAddPage(owner,state,{L"Binding cache "+std::to_wstring(i),true,NotesPlainRtf(L"Cache eviction document"),{}}),"Native binding cache fixture creates a saved note");
  Check(!state.editorLinks.contains(noteId),"Eviction releases the old editor's URL registry");
  Check(NotesOpenSavedPage(owner,state,index(noteId)),"Evicted linked note reopens from its saved snapshot");
  click(L"https://example.test/shared-D",6,9);
  Check(NotesNativeLinkAddress(Edit(owner),state,{0,3}).empty() && !SendMessageW(Edit(owner),EM_CANUNDO,0,0),"Eviction rebuilds only current links and starts the documented fresh Undo history");
  Fixture(L"Cookie collision QA");auto other=state.pages[state.activePage];other.open=other.active=true;other.links={{L"One",L"https://example.test/other-client"}};
  const auto otherRoot=g_sDataDir/L"Sites"/L"Cookie collision QA";Check(client_notes::WriteNotebook(otherRoot,{other},client_notes::ReadNotebook(otherRoot)),"Other client fixture deliberately uses the same stable note identity");
  Check(SwitchClientNotes(owner,state,L"Cookie collision QA",otherRoot),"Client switch loads a colliding note identity safely");click(L"https://example.test/other-client",6,9);
  Check(SwitchClientNotes(owner,state,L"Link Undo bindings QA",g_sDataDir/L"Sites"/L"Link Undo bindings QA"),"Returning to the original client uses its own URL registry");
  Check(NotesOpenSavedPage(owner,state,index(noteId)),"Original linked note remains available after the colliding-client switch");click(L"https://example.test/shared-D",6,9);
  Command(owner,IDC_NOTES_CLOSE_TAB);Check(!state.editorLinks.contains(noteId),"Closing a linked tab releases its registry");
  Check(NotesOpenSavedPage(owner,state,index(noteId)),"Closed linked note can rebuild its current target");click(L"https://example.test/shared-D",6,9);
  CloseClientNotesDialog(owner,state);owner=Open(L"Link Undo bindings QA");
  Check(NotesWindowText(Edit(owner))==L"One / One" && NotesNativeLinkAddress(Edit(owner),State(owner),{6,9})==L"https://example.test/shared-D",
      "Application restart preserves the current address without serialized native cookie IDs");
  DestroyWindow(owner);

  Fixture(L"Link registry bound QA");owner=Open(L"Link registry bound QA");Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Keep / Edit");
  auto &bounded=State(owner);
  Select(Edit(owner),0,4);inputs.push_back({L"Keep",L"https://example.test/old-live"});Command(owner,IDC_NOTES_LINK);Save(owner);
  Select(Edit(owner),7,11);inputs.push_back({L"Edit",L"https://example.test/revision-0"});Command(owner,IDC_NOTES_LINK);Save(owner);
  for(int revision=1;revision<=650;++revision) {
    Select(Edit(owner),7,11);inputs.push_back({L"Edit",L"https://example.test/revision-"+std::to_wstring(revision)});Command(owner,IDC_NOTES_LINK);
    if(!SaveClientNotesDraft(owner,bounded,true)) throw std::runtime_error("Repeated URI revision save failed");
  }
  Check(bounded.editorLinks[bounded.attachedPageId].urls.size()<=512,"Hundreds of URL edits keep the native binding registry bounded");
  Select(Edit(owner),0,4);inputs.push_back({L"Keep",L"https://example.test/new-keep"});Command(owner,IDC_NOTES_LINK);Save(owner);Command(owner,IDC_NOTES_UNDO);
  Check(NotesNativeLinkAddress(Edit(owner),bounded,{0,4})==L"https://example.test/old-live","An old live target remains undoable after registry pruning");
  for(int i=0;i<99;++i) Command(owner,IDC_NOTES_UNDO);
  Check(NotesNativeLinkAddress(Edit(owner),bounded,{7,11})==L"https://example.test/revision-551","The supported 100 native Undo actions retain their precise older URI bindings after pruning");
  Save(owner);DestroyWindow(owner);

  Fixture(L"Adjacent link binding QA");owner=Open(L"Adjacent link binding QA");Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"SupportPortal");
  link(L"Support",L"https://example.test/adjacent-A",0,7);link(L"Portal",L"https://example.test/adjacent-B",7,13);
  click(L"https://example.test/adjacent-A",0,7);click(L"https://example.test/adjacent-B",7,13);
  Check(State(owner).pages[State(owner).activePage].links.size()==2,"Adjacent links with identical visible formatting retain separate native cookie ranges");
  Save(owner);CloseClientNotesDialog(owner,State(owner));owner=Open(L"Adjacent link binding QA");
  click(L"https://example.test/adjacent-A",0,7);click(L"https://example.test/adjacent-B",7,13);
  Select(Edit(owner),13,13);SendMessageW(Edit(owner),WM_CHAR,VK_RETURN,0);Type(owner,L"Plain text");Save(owner);
  Check(NotesNativeLinkAddress(Edit(owner),State(owner),{14,24}).empty() && State(owner).pages[State(owner).activePage].links.size()==2,
      "Enter at a link boundary and subsequent text do not inherit the link target");
  LONG missing=0;bool nativeOk=true;NotesRangeLinkEffects(nullptr,{0,1},nullptr,&missing,false,&nativeOk);
  Check(!nativeOk && missing==LONG_MIN && !NotesBindLinkCookie(nullptr,State(owner),State(owner).attachedPageId,{0,1},1),
      "Unavailable native cookie APIs fail explicitly without a zero or newer-address fallback");
  DestroyWindow(owner);

  NotesBulkLinkUndoBindings();
}
static void NotesBulkLinkUndoBindings() {
  Fixture(L"Bulk link Undo QA");HWND owner=Open(L"Bulk link Undo QA");Command(owner,IDC_NOTES_ADD_TAB);
  std::wstring body;for(int i=0;i<256;++i) {wchar_t label[8]{};swprintf_s(label,L"L%03d",i);body+=label;body+=L" ";}Type(owner,body.c_str());
  auto &bulk=State(owner);
  const auto bulkEdit=[&](int i,const std::wstring &url) {const auto label=body.substr(i*5,4);Select(Edit(owner),i*5,i*5+4);inputs.push_back({label,url});Command(owner,IDC_NOTES_LINK);};
  for(int i=0;i<256;++i) bulkEdit(i,L"https://example.test/bulk-"+std::to_wstring(i));
  Save(owner);
  const auto unchangedCookie=bulk.editorLinks[bulk.attachedPageId].nextCookie;
  Select(Edit(owner),1280,1280);inputs.push_back({L"New target",L"https://example.test/over-limit"});Command(owner,IDC_NOTES_LINK);
  Check(NotesWindowText(Edit(owner))==body && bulk.editorLinks[bulk.attachedPageId].nextCookie==unchangedCookie && lastNotice.find(L"256 link targets")!=std::wstring::npos,"A genuinely new257th target is explained and rejected without changing text or native cookies");
  Select(Edit(owner),1276,1278);inputs.push_back({L"X",L"https://example.test/partial-over-limit"});Command(owner,IDC_NOTES_LINK);
  Check(NotesWindowText(Edit(owner))==body && NotesNativeLinkAddress(Edit(owner),bulk,{1275,1279})==L"https://example.test/bulk-255","Partial replacement counts surviving prefix/suffix targets and preserves the existing link on overflow");
  LONG originalPartialCookie=0;NotesRangeLinkEffects(Edit(owner),{1275,1279},nullptr,&originalPartialCookie);
  Select(Edit(owner),1276,1278);inputs.push_back({L"R",L"https://example.test/bulk-255"});Command(owner,IDC_NOTES_LINK);
  const auto partialText=NotesWindowText(Edit(owner));auto expectedPartial=body;expectedPartial.replace(1276,2,L"R");
  bool allPartialPieces=true;
  const wchar_t *partialNames[]={L"prefix",L"replacement",L"suffix"};
  const wchar_t *partialExpected[]={L"L",L"R",L"5"};
  {notes_undo::NativeDocument partialDocument(Edit(owner));
  for(LONG piece=0;piece<3;++piece) {
    const CHARRANGE range{1275+piece,1276+piece};LONG effects=0,cookie=0;
    const bool read=notes_undo::RangeFont(partialDocument.value,range.cpMin,range.cpMax,effects,cookie);
    const auto text=NotesRangeText(Edit(owner),range.cpMin,range.cpMax),url=NotesNativeLinkAddress(Edit(owner),bulk,range);
    const bool registered=bulk.editorLinks[bulk.attachedPageId].urls.contains(cookie);
    std::wcout<<L"Partial same-URL native "<<partialNames[piece]<<L": range="<<range.cpMin<<L","<<range.cpMax
      <<L", text="<<text<<L", read="<<read<<L", effects="<<effects<<L", cookie="<<cookie
      <<L", registered="<<registered<<L", actual="<<url<<std::endl;
    allPartialPieces=allPartialPieces && read && text==partialExpected[piece] && (effects&CFE_LINK)!=0 &&
        cookie==originalPartialCookie && cookie>0 && registered && url==L"https://example.test/bulk-255";
  }
  }
  std::wcout<<L"Partial same-URL complete: wholeText="<<(partialText==expectedPartial)<<L", merged="
    <<NotesNativeLinkAddress(Edit(owner),bulk,{1275,1278})<<L", allPieces="<<allPartialPieces<<std::endl;
  Check(partialText.substr(1275,3)==L"LR5" && NotesNativeLinkAddress(Edit(owner),bulk,{1275,1278})==L"https://example.test/bulk-255","A partial same-URL edit merges its surviving adjacent pieces and remains allowed at the limit");
  Check(partialText==expectedPartial && allPartialPieces,"Partial same-URL replacement preserves exact full text and native prefix/replacement/suffix cookie effects and URLs");
  Save(owner);const auto &partialPage=bulk.pages[bulk.activePage];
  const bool savedMerged=std::any_of(partialPage.linkRanges.begin(),partialPage.linkRanges.end(),[&](const auto &range) {
    return range.first==1275 && range.last==1278 && range.target<partialPage.links.size() &&
        partialPage.links[range.target]==std::pair<std::wstring,std::wstring>{L"LR5",L"https://example.test/bulk-255"};
  });
  Check(partialPage.links.size()==256 && partialPage.linkRangesKnown && savedMerged,"Saved partial same-URL mapping spans all three pieces at exactly 256 targets");
  Command(owner,IDC_NOTES_UNDO);
  Check(NotesWindowText(Edit(owner))==body && NotesNativeLinkAddress(Edit(owner),bulk,{1275,1279})==L"https://example.test/bulk-255","One native Undo restores the complete original partial-link text and immutable URL");Save(owner);
  Select(Edit(owner),1280,1280);inputs.push_back({L"L255",L"https://example.test/bulk-255"});Command(owner,IDC_NOTES_LINK);Save(owner);
  Check(bulk.pages[bulk.activePage].links.size()==256 && NotesNativeLinkAddress(Edit(owner),bulk,{1280,1284})==L"https://example.test/bulk-255","A repeated occurrence reuses its existing target at the full limit");
  Select(Edit(owner),1275,1279);inputs.push_back({L"L255",L"https://example.test/shared-over-limit"});Command(owner,IDC_NOTES_LINK);
  Check(NotesNativeLinkAddress(Edit(owner),bulk,{1275,1279})==L"https://example.test/bulk-255" && NotesNativeLinkAddress(Edit(owner),bulk,{1280,1284})==L"https://example.test/bulk-255","An old target that survives in another occurrence is not incorrectly released by replacement budgeting");Command(owner,IDC_NOTES_UNDO);Save(owner);
  Check(NotesWindowText(Edit(owner))==body,"Rejected target edits leave the existing native Undo action intact");
  client_notes::NotePage snippet{L"Limit replacement",true,NotesPlainRtf(L"L255"),{}};snippet.links={{L"L255",L"https://example.test/snippet-limit"}};snippet.linkRangesKnown=true;snippet.linkRanges={{0,4,0}};
  NotesTemplates snippets{owner,&bulk};snippets.snippets=true;Select(Edit(owner),1275,1279);
  Check(NotesInsertSnippet(owner,snippets,snippet) && bulk.pages[bulk.activePage].links.size()==256 && NotesNativeLinkAddress(Edit(owner),bulk,{1275,1279})==L"https://example.test/snippet-limit","Replacing a whole link with a snippet releases its old target and fits the same256-target limit");Command(owner,IDC_NOTES_UNDO);Save(owner);
  Check(NotesWindowText(Edit(owner))==body && NotesNativeLinkAddress(Edit(owner),bulk,{1275,1279})==L"https://example.test/bulk-255","Snippet replacement at the limit preserves precise native Undo");
  for(int revision=1;revision<=650;++revision) bulkEdit(255,L"https://example.test/bulk-revision-"+std::to_wstring(revision));
  Save(owner);
  LONG revisedCookie=0;NotesRangeLinkEffects(Edit(owner),{1275,1279},nullptr,&revisedCookie);
  Check(revisedCookie>650 && NotesNativeLinkAddress(Edit(owner),bulk,{1275,1279})==L"https://example.test/bulk-revision-650","All650 link-target revisions actually apply at the full256-target limit before Undo is tested");
  Check(bulk.editorLinks[bulk.attachedPageId].urls.size()<=512 && bulk.pages[bulk.activePage].links.size()==256,"Pruning retains 256 live targets after hundreds of unrelated revisions");
  Select(Edit(owner),0,-1);Command(owner,IDC_NOTES_CLEAR);Save(owner);
  Check(bulk.pages[bulk.activePage].links.empty(),"One grouped Clear action removes all 256 live link intents");
  Command(owner,IDC_NOTES_UNDO);
  for(const CHARRANGE range: {CHARRANGE{0,4},CHARRANGE{1275,1279}}) {LONG cookie=0;const bool linked=NotesRangeLinkEffects(Edit(owner),range,nullptr,&cookie);std::wcout<<L"Bulk immediate Undo: first="<<range.cpMin<<L", cookie="<<cookie<<L", linked="<<linked<<L", registered="<<bulk.editorLinks[bulk.attachedPageId].urls.contains(cookie)<<L", actual="<<NotesNativeLinkAddress(Edit(owner),bulk,range)<<L", canUndo="<<SendMessageW(Edit(owner),EM_CANUNDO,0,0)<<std::endl;}
  Check(NotesNativeLinkAddress(Edit(owner),bulk,{0,4})==L"https://example.test/bulk-0" && NotesNativeLinkAddress(Edit(owner),bulk,{1275,1279})==L"https://example.test/bulk-revision-650",
      "The 256-link Clear restores its first and last native cookie in one Undo action");
  Command(owner,IDC_NOTES_REDO);Check(NotesNativeLinkAddress(Edit(owner),bulk,{0,4}).empty(),"Redo of the bulk Clear preserves explicitly removed link intent");
  for(int revision=1;revision<=99;++revision) bulkEdit(255,L"https://example.test/after-clear-"+std::to_wstring(revision));
  for(int i=0;i<100;++i) Command(owner,IDC_NOTES_UNDO);
  bool restored=true;for(int i=0;i<256;++i) {
    const auto expected=i==255 ? L"https://example.test/bulk-revision-650" : L"https://example.test/bulk-"+std::to_wstring(i);
    restored=restored && NotesNativeLinkAddress(Edit(owner),bulk,{i*5,i*5+4})==expected;
  }
  if(!restored) {LONG cookie=0;const bool linked=NotesRangeLinkEffects(Edit(owner),{0,4},nullptr,&cookie);std::wcout<<L"Bulk Undo mismatch: first cookie "<<cookie<<L", linked "<<linked<<L", URI "<<NotesNativeLinkAddress(Edit(owner),bulk,{0,4})<<L", last URI "<<NotesNativeLinkAddress(Edit(owner),bulk,{1275,1279})<<L", can Undo "<<SendMessageW(Edit(owner),EM_CANUNDO,0,0)<<L", registry "<<bulk.editorLinks[bulk.attachedPageId].urls.size()<<std::endl;}
  Check(restored,"A 256-link grouped Clear remains precisely undoable after 99 newer URI actions and registry pruning");
  Select(Edit(owner),0,-1);Type(owner,L"Pasted ordinary replacement");Command(owner,IDC_NOTES_UNDO);
  Check(NotesWindowText(Edit(owner))==body && NotesNativeLinkAddress(Edit(owner),bulk,{0,4})==L"https://example.test/bulk-0",
      "Replacing a whole linked document and Undo restores its native URL intent");
  for(int revision=1;revision<=650;++revision) bulkEdit(255,L"https://example.test/before-code-"+std::to_wstring(revision));
  Select(Edit(owner),0,-1);NotesCodeInput code;code.language=4;code.source=body;
  Check(NotesInsertCode(owner,bulk,code) && !bulk.saveFailed && bulk.pages[bulk.activePage].links.empty(),
      "Replacing friendly links with a code box autosaves its table without inheriting their URLs");
  for(int revision=1;revision<=99;++revision) {
    const auto label=body.substr(1275,4);FINDTEXTEXW find{{0,-1},label.c_str(),{}};
    if(SendMessageW(Edit(owner),EM_FINDTEXTEXW,FR_DOWN|FR_MATCHCASE,reinterpret_cast<LPARAM>(&find))<0) throw std::runtime_error("Code Undo fixture label missing");
    Select(Edit(owner),find.chrgText.cpMin,find.chrgText.cpMax);inputs.push_back({label,L"https://example.test/inside-code-"+std::to_wstring(revision)});Command(owner,IDC_NOTES_LINK);
  }
  Save(owner);
  Check(bulk.pages[bulk.activePage].links.size()==1,"A friendly link inside a code table captures only its exact text and target");
  for(int i=0;i<100;++i) Command(owner,IDC_NOTES_UNDO);
  restored=true;for(int i=0;i<256;++i) {
    const auto expected=i==255 ? L"https://example.test/before-code-650" : L"https://example.test/bulk-"+std::to_wstring(i);
    restored=restored && NotesNativeLinkAddress(Edit(owner),bulk,{i*5,i*5+4})==expected;
  }
  Check(NotesWindowText(Edit(owner))==body && restored,"Undo of an RTF code replacement retains all 256 original URL bindings after pruning and 99 later edits");
  Save(owner);DestroyWindow(owner);
}
static void NotesSnippetLinkLimits(bool rangeLimit=false) {
  const auto name=rangeLimit ? L"Snippet range limit QA" : L"Snippet caption limit QA";Fixture(name);HWND owner=Open(name);auto &state=State(owner);
  client_notes::NotePage original{L"Bounded snippet destination",true,{},{}};std::wstring body;
  if(rangeLimit) {for(size_t i=0;i<client_notes::kMaxLinkRanges;++i){body+=L"A ";original.linkRanges.push_back({static_cast<uint32_t>(i*2),static_cast<uint32_t>(i*2+1),0});}original.links={{L"A",L"https://example.test/snippet-bound"}};}
  else {body.assign(2048,L'A');original.links={{body,L"https://example.test/snippet-bound"}};original.linkRanges={{0,2048,0}};}
  original.rtf=NotesPlainRtf(body);original.linkRangesKnown=true;
  Check(NotesAddPage(owner,state,std::move(original)),"The bounded snippet destination opens and saves its validated native link occurrences");
  const auto before=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes;const auto cookie=state.editorLinks[state.attachedPageId].nextCookie;
  client_notes::NotePage snippet{L"One linked letter",true,NotesPlainRtf(L"A"),{}};snippet.links={{L"A",L"https://example.test/snippet-bound"}};snippet.linkRangesKnown=true;snippet.linkRanges={{0,1,0}};
  NotesTemplates library{owner,&state};library.snippets=true;Select(Edit(owner),static_cast<LONG>(body.size()),static_cast<LONG>(body.size()));
  const bool inserted=NotesInsertSnippet(owner,library,snippet);
  std::cout<<"Snippet bound diagnostic: ranges="<<rangeLimit<<", inserted="<<inserted<<", before chars="<<body.size()<<", after chars="<<NotesWindowText(Edit(owner)).size()<<", saveFailed="<<state.saveFailed<<std::endl;
  Check(!inserted && NotesWindowText(Edit(owner))==body && !state.saveFailed && state.editorLinks[state.attachedPageId].nextCookie==cookie && client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==before,
      rangeLimit ? "A low-target-count snippet that would exceed16,384 native linked ranges is rejected before changing editor or disk" : "A low-target-count adjacent snippet that would exceed a2,048-character link caption is rejected before changing editor or disk");
  if(!rangeLimit) {
    Command(owner,IDC_NOTES_ADD_TAB);client_notes::NotePage legacy{L"Legacy friendly snippet",true,"{\\rtf1\\ansi\\ul Legacy\\ulnone}",{}};legacy.links={{L"Legacy",L"https://example.test/legacy-snippet-bound"}};
    Check(NotesInsertSnippet(owner,library,legacy) && NotesNativeLinkAddress(Edit(owner),state,{0,6})==L"https://example.test/legacy-snippet-bound","Prospective snippet validation preserves a safely resolvable legacy friendly-link occurrence");
  }
  DestroyWindow(owner);
}

static void NotesAuditEditorCache(bool supportedLimit) {
  Fixture(L"Cache regression QA");const auto root=g_sDataDir/L"Sites/Cache regression QA";
  std::vector<client_notes::NotePage> pages;
  const size_t count=supportedLimit ? client_notes::kMaxPages : 96;
  for (size_t i=0;i<count;++i) {
    client_notes::NotePage page{L"Resource note "+std::to_wstring(i+1),true,NotesPlainRtf(L"Distinct saved document "+std::to_wstring(i+1)),{}};
    page.id=i+1;page.open=!supportedLimit || i==0;page.active=i==0;pages.push_back(std::move(page));
  }
  Check(client_notes::WriteNotebook(root,pages,client_notes::ReadNotebook(root)),"Cache fixture commits valid saved notes");
  HWND owner=Open(L"Cache regression QA");auto &state=State(owner);const auto baseline=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
  Select(Edit(owner),2,8);const HWND first=Edit(owner);
  for (size_t i=0;i<count;++i) {
    state.pages[i].open=true;state.activePage=i;
    if (!LoadNotesPage(owner,state)) throw std::runtime_error("Bounded cache failed to load a supported saved note");
    if (state.editors.size()>kNotesEditorCacheLimit) throw std::runtime_error("Editor cache exceeded native resource bound");
    if (supportedLimit) NotesClosePage(owner,state,i);
    if (i && i%1000==0) std::cout<<"Cache stress visited "<<i<<", retained "<<state.editors.size()<<", USER "<<GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)<<std::endl;
  }
  Check(state.editors.size()<=kNotesEditorCacheLimit && GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)<=baseline+20,
      "Opening supported notes stays under the bounded editor and USER-resource budget");
  if (supportedLimit) {
    NotesSelectAvailable(owner,state);
    Check(state.editors.empty() && !state.attachedEditor,"Closing every visited note releases every editor");
  } else {
    Check(!IsWindow(first) && state.editorViews.contains(1),"Old open tabs evict clean editors while retaining their selection and scroll snapshot");
    state.activePage=0;Check(LoadNotesPage(owner,state),"An evicted open note can be loaded again");
    CHARRANGE selection{};NotesSelection(Edit(owner),selection);
    Check(selection.cpMin==2 && selection.cpMax==8 && NotesWindowText(Edit(owner))==L"Distinct saved document 1", "Evicted open note restores full selection and its correct saved document");
    const HWND recent=Edit(owner);Type(owner,L"Fresh edit");Save(owner);const auto undo=SendMessageW(recent,EM_CANUNDO,0,0);
    state.activePage=1;LoadNotesPage(owner,state);state.activePage=0;LoadNotesPage(owner,state);
    Check(Edit(owner)==recent && undo && SendMessageW(recent,EM_CANUNDO,0,0),"Active and recent open tabs retain their native undo history");
  }
  DestroyWindow(owner);
}

#include "NotesPlusSwitchTests.inl"
static void NotesAddTabRedraw() {
  Fixture(L"Add button redraw QA");const auto root=g_sDataDir/L"Sites/Add button redraw QA";
  std::vector<client_notes::NotePage> pages;
  for(const wchar_t *name:{L"Printer rollout",L"Work log",L"Contacts"}) {
    client_notes::NotePage page{name,true,NotesPlainRtf(name),{}};
    page.id=pages.size()+1;page.active=page.id==1;pages.push_back(std::move(page));
  }
  Check(client_notes::WriteNotebook(root,pages,client_notes::ReadNotebook(root)),"Add-button redraw fixture saves three notes");
  HWND owner=Open(L"Add button redraw QA");auto &state=State(owner);HWND button=GetDlgItem(owner,IDC_NOTES_ADD_TAB);
  const UINT dpi=GetDpiForWindow(owner);
  SetWindowPos(owner,nullptr,0,0,ScaleByDpi(1150,dpi),ScaleByDpi(720,dpi),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
  const auto bounds=[&] {RECT r{};GetWindowRect(button,&r);return r;};
  for(bool adding:{true,false}) {
    const RECT before=bounds();HRGN repaint=CreateRectRgn(0,0,0,0);
    struct CaptureReset {~CaptureReset(){notesInvalidationCaptureWindow=nullptr;notesInvalidationCaptureRegion=nullptr;notesInvalidationCaptureErase=false;}} reset;
    notesInvalidationCaptureWindow=button;notesInvalidationCaptureRegion=repaint;notesInvalidationCaptureErase=false;
    Command(owner,adding ? IDC_NOTES_ADD_TAB : IDC_NOTES_CLOSE_TAB);
    notesInvalidationCaptureWindow=nullptr;notesInvalidationCaptureRegion=nullptr;
    const RECT after=bounds();RECT area{};GetClientRect(button,&area);
    Check(before.left!=after.left && TabCtrl_GetItemCount(GetDlgItem(owner,IDC_NOTES_TABS))==(adding ? 4 : 3),"Adding or closing the fourth tab moves the same plus button beside the final strip");
    Check(!notesInvalidationCaptureErase && PtInRegion(repaint,0,area.bottom/2) && PtInRegion(repaint,area.right-1,area.bottom/2),"A relocated plus queues both vertical edges for non-erasing repaint after native positioning");
    HDC native=GetDC(button),dc=CreateCompatibleDC(native);HBITMAP bitmap=CreateCompatibleBitmap(native,area.right,area.bottom);
    Check(dc && bitmap,"The relocated plus uses a private native-size render surface");const auto oldBitmap=SelectObject(dc,bitmap);
    HBRUSH sentinel=CreateSolidBrush(RGB(255,0,255));FillRect(dc,&area,sentinel);DeleteObject(sentinel);SelectClipRgn(dc,repaint);
    DRAWITEMSTRUCT draw{ODT_BUTTON,IDC_NOTES_ADD_TAB,0,ODA_DRAWENTIRE,0,button,dc,area,0};
    const auto previousHot=g_hHotButton;g_hHotButton=nullptr;
    Check(DrawNotesToolbarButton(draw,state),"The production plus renderer handles the queued native button paint");GdiFlush();
    const COLORREF left=GetPixel(dc,0,area.bottom/2),right=GetPixel(dc,area.right-1,area.bottom/2);
    Check(left==right && left!=NotesWindowColor() && left!=RGB(255,0,255),"The queued full button render gives both plus edges the same complete accent outline");
    g_hHotButton=previousHot;SelectClipRgn(dc,nullptr);SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(button,native);DeleteObject(repaint);
  }
  HRGN unchanged=CreateRectRgn(0,0,0,0);notesInvalidationCaptureWindow=button;notesInvalidationCaptureRegion=unchanged;
  LayoutClientNotes(owner,dpi);notesInvalidationCaptureWindow=nullptr;notesInvalidationCaptureRegion=nullptr;
  RECT empty{};Check(GetRgnBox(unchanged,&empty)==NULLREGION,"An unchanged Notes layout does not repeatedly invalidate the plus button");DeleteObject(unchanged);
  DestroyWindow(owner);
  Check(notes_plus_switch::Run(g_sDataDir/L"plus-switch-captures"),"The native plus-switch, resize and saved-client-isolation matrix completes without missing edges");
}

static void NotesLibraryResizeBorders() {
  Fixture(L"Library border QA");HWND owner=Open(L"Library border QA");auto &state=State(owner);
  Type(owner,L"Library resize must preserve this note.");Save(owner);
  for(int filter:{0,3}) {
    NotesLibrary library{owner,&state};library.initialFilter=filter;
    HWND window=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&library));
    Check(window!=nullptr,"The library border regression opens the production All notes or Trash dialog");
    Check((GetWindowLongPtrW(window,GWL_STYLE)&WS_CLIPCHILDREN)!=0,"Library background painting excludes native child surfaces");
    const UINT dpi=GetDpiForWindow(window);const auto px=[&](int n){return ScaleByDpi(n,dpi);};
    SetWindowPos(window,nullptr,0,0,px(1000),px(690),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    RECT before{};GetClientRect(window,&before);
    const auto bounds=[&](int id) {RECT r{};GetWindowRect(GetDlgItem(window,id),&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT *>(&r),2);return r;};
    const RECT oldQuery=bounds(IDC_NOTES_QUERY),oldList=bounds(IDC_NOTES_RESULTS);
    const RECT surface{0,0,before.right+px(700),before.bottom+px(400)};
    HDC native=GetDC(window),dc=CreateCompatibleDC(native);
    HBITMAP bitmap=CreateCompatibleBitmap(native,surface.right,surface.bottom);
    Check(dc && bitmap,"Library border regression allocates its private offscreen surface");
    const auto oldBitmap=SelectObject(dc,bitmap);FillRect(dc,&surface,g_hbrThemeWindow);
    HRGN oldArea=CreateRectRgnIndirect(&before);SelectClipRgn(dc,oldArea);
    NotesLibraryProc(window,WM_ERASEBKGND,reinterpret_cast<WPARAM>(dc),0);
    // Preserve the previous parent image and model only the newly exposed
    // strips. A full PrintWindow/erase here would hide the resize defect.
    HRGN repaint=CreateRectRgnIndirect(&surface);CombineRgn(repaint,repaint,oldArea,RGN_DIFF);
    struct CaptureReset {~CaptureReset(){notesInvalidationCaptureWindow=nullptr;notesInvalidationCaptureRegion=nullptr;notesInvalidationCaptureErase=false;}} reset;
    notesInvalidationCaptureWindow=window;notesInvalidationCaptureRegion=repaint;notesInvalidationCaptureErase=false;
    SetWindowPos(window,nullptr,0,0,px(1700),px(1090),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    notesInvalidationCaptureWindow=nullptr;notesInvalidationCaptureRegion=nullptr;
    const RECT query=bounds(IDC_NOTES_QUERY),list=bounds(IDC_NOTES_RESULTS);
    const int gapX=(oldQuery.right+(std::min)(query.right,before.right))/2;
    const int gapY=(oldList.bottom+(std::min)(list.bottom,before.bottom))/2;
    const POINT samples[]={{gapX,query.top-1},{gapX,query.bottom},{list.left-1,gapY},{list.right,gapY}};
    bool oldPixels=true,queued=true;
    for(const auto &point:samples) {
      oldPixels=oldPixels && PtInRect(&before,point) && GetPixel(dc,point.x,point.y)!=g_themeColors.crControlBorder;
      queued=queued && PtInRegion(repaint,point.x,point.y)!=FALSE;
    }
    Check(oldPixels,"Border samples lie in previously valid parent pixels that the old native layout does not repaint");
    Check(queued && notesInvalidationCaptureErase,"Growing the library queues an erase covering new outlines inside its old footprint");
    SelectClipRgn(dc,repaint);
    NotesLibraryProc(window,WM_ERASEBKGND,reinterpret_cast<WPARAM>(dc),0);GdiFlush();
    bool complete=true;for(const auto &point:samples) complete=complete && GetPixel(dc,point.x,point.y)==g_themeColors.crControlBorder;
    Check(complete,"The actual coalesced parent erase restores search top/bottom and both list edges after growth");
    Check(NotesWindowText(Edit(owner))==L"Library resize must preserve this note.","Library resizing leaves the client note unchanged");
    SelectClipRgn(dc,nullptr);SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(window,native);
    DeleteObject(oldArea);DeleteObject(repaint);DestroyWindow(window);
  }
  DestroyWindow(owner);
}

static void NotesLibrarySearchDecoder() {
  Fixture(L"Search decoder QA");const auto root=g_sDataDir/L"Sites/Search decoder QA";
  std::vector<client_notes::NotePage> pages;
  for(size_t i=0;i<1000;++i) {
    const std::wstring text=i%97==0 ? L"NeedleXYZ \x4e2d\x6587 \xD83D\xDE80\r\nNext line" : L"Ordinary search fixture content";
    client_notes::NotePage page{L"Decoder "+std::to_wstring(i),true,NotesPlainRtf(text),{}};
    page.id=i+1;page.open=page.active=false;pages.push_back(std::move(page));
  }
  Check(client_notes::WriteNotebook(root,pages,client_notes::ReadNotebook(root)),"Search decoder fixture saves 1,000 closed rich notes");
  HWND owner=Open(L"Search decoder QA");auto &state=State(owner);NotesLibrary library{owner,&state};
  HWND window=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&library));
  Check(window && library.results.size()==1000,"Large library opens without preparing unused search editors");
  const auto before=explicitRichEditCreates;
  SetDlgItemTextW(window,IDC_NOTES_QUERY,L"needlexyz");SendMessageW(window,WM_TIMER,1,0);
  Check(library.results.size()==11 && library.plain.size()==1000,"Cold search decodes every note and finds all expected body matches");
  Check(explicitRichEditCreates-before==1 && library.plainEditor && IsWindow(library.plainEditor),"A cold 1,000-note search creates exactly one reusable native decoder");
  const HWND extractor=library.plainEditor;
  Check(GetParent(extractor)==window && !(GetWindowLongPtrW(extractor,GWL_STYLE)&WS_VISIBLE),"Search decoder belongs to its library and stays invisible");
  SetDlgItemTextW(window,IDC_NOTES_QUERY,L"\x4e2d\x6587");SendMessageW(window,WM_TIMER,1,0);
  Check(library.results.size()==11 && library.plainEditor==extractor && explicitRichEditCreates-before==1,"Warm Unicode search reuses cached text and the same decoder");
  const std::vector<std::string> rtfCases={
    NotesPlainRtf(L"Braces { } and \\; \xD83D\xDE80\r\n\t\x4e2d\x6587"),
    "{\\rtf1\\ansi\\ansicpg1252\\uc1 Caf\\'e9\\par second}",
    "{\\rtf1\\ansi\\ansicpg1251 \\'cf\\'f0\\'e8\\'e2\\'e5\\'f2}",
    "{\\rtf1\\ansi{\\fonttbl{\\f0 Arial;}{\\f1 Courier New;}}\\f1 code\\tab value\\line next\\f0 normal}",
    "{\\rtf1\\ansi\\trowd\\cellx1000\\cellx2000\\intbl One\\cell Two\\cell\\row}",
    NotesPlainRtf(L"")};
  for(const auto &rtf:rtfCases) {
    client_notes::NotePage page{L"Decoder equivalence",true,rtf,{}};
    Check(NotesPlain(page,extractor)==NotesPlain(page),"Reused native decoder matches fresh RTF extraction across Unicode, code pages and tables");
  }
  const auto untouched=client_notes::ReadNotebook(root);
  Check(untouched.pages.size()==pages.size() && untouched.pages.front().rtf==pages.front().rtf && state.editors.empty(),"Search does not mutate saved content or create open-tab editors");
  DestroyWindow(window);
  Check(!IsWindow(extractor) && !library.plainEditor,"Closing the library releases and clears its search decoder");
  DestroyWindow(owner);
}

#include "NotesBackupTests.inl"
#include "NotesPersistenceTests.inl"
#include "NotesPersistenceRound4Tests.inl"
#include "NotesGeometryTests.inl"
#include "NotesBackgroundWorkflows.inl"
#include "NotesProductivityRound4Tests.inl"
#include "NotesRedrawTests.inl"
#include "NotesNormalOpeningTests.inl"
#include "NotesRollbackTransactionsTests.inl"
#include "NotesCodeRecoveryTests.inl"

int wmain(int argc,wchar_t **argv) {
  try {
    const bool explicitBackground=argc>1 && !wcscmp(argv[1],L"--headless-background");
    if(explicitBackground && (argc<3 || !NotesBackgroundSelector(argv[2]))) {
      std::cerr<<"Background argument refused before native initialization; native windows created=0\n";return 2;
    }
    if(explicitBackground){--argc;++argv;}
    backgroundNotesTests=argc>1 && NotesBackgroundSelector(argv[1]);
    NotesBackgroundEvents backgroundEvents;if(backgroundNotesTests) backgroundEvents.Start();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES}; InitCommonControlsEx(&controls);
    Gdiplus::GdiplusStartupInput graphics; Gdiplus::GdiplusStartup(&g_gdiplusToken,&graphics,nullptr);
    g_hInst=GetModuleHandleW(nullptr); g_bThemeIsDark=true; BuildThemeColors(true,g_themeColors); UpdateThemeBrushes();
    g_sDataDir=fs::current_path()/L"build"/(L"notes-workspace-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    fs::create_directories(g_sDataDir); g_sConfigPath=g_sDataDir/L"ctSpaces.ini";
    Fixture(L"Workspace QA"); Fixture(L"Destination QA");
    g_hGui=CreateWindowExW(0,L"STATIC",L"Notes test parent",WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,g_hInst,nullptr);
    if(backgroundNotesTests) NotesBackgroundAudit("initialization");
    if(argc>1 && !wcscmp(argv[1],L"--normal-opening")) {NotesNormalOpening();DestroyWindow(g_hGui);NotesBackgroundAudit("normal Notes opening cleanup");NotesBackgroundBanner();std::cout<<"Notes normal opening: "<<checks<<" checks passed.\n";return 0;}
    if(argc>1 && !wcscmp(argv[1],L"--plus-switch-background")) {const bool passed=notes_plus_switch::Run(argc>2?fs::absolute(argv[2]):g_sDataDir/L"plus-switch-captures");DestroyWindow(g_hGui);NotesBackgroundAudit("plus-switch native proof cleanup");NotesBackgroundBanner();std::cout<<"Notes plus switch: "<<checks<<" setup checks; "<<notes_plus_switch::checks<<" native checks; "<<notes_plus_switch::failures<<" failures.\n";return passed?0:1;}
    if(argc>1 && !wcscmp(argv[1],L"--rollback-transactions-background")) {NotesRollbackTransactions(argc>2?fs::absolute(argv[2]):fs::path{});DestroyWindow(g_hGui);NotesBackgroundAudit("rollback transaction native proof cleanup");NotesBackgroundBanner();std::cout<<"Notes rollback transactions: "<<checks<<" checks passed.\n";return 0;}
    if(argc>1 && !wcscmp(argv[1],L"--code-rollback-background")) {NotesCodeRollbackTransactions(argc>2?fs::absolute(argv[2]):fs::path{});DestroyWindow(g_hGui);NotesBackgroundAudit("Code rollback native proof cleanup");NotesBackgroundBanner();std::cout<<"Notes Code rollback: "<<checks<<" checks passed.\n";return 0;}
    if(argc>1 && !wcscmp(argv[1],L"--library-resize-border")) {NotesLibraryResizeBorders();DestroyWindow(g_hGui);NotesBackgroundAudit("library resize border cleanup");NotesBackgroundBanner();std::cout<<"Notes library resize borders: "<<checks<<" checks passed.\n";return 0;}
    if(argc>1 && !wcscmp(argv[1],L"--tab-tooltip-lifetime")) {NotesTabTooltipLifetime();DestroyWindow(g_hGui);NotesBackgroundAudit("tab tooltip lifetime cleanup");NotesBackgroundBanner();std::cout<<"Notes tab tooltip lifetime: "<<checks<<" checks passed.\n";return 0;}
    if(argc>1 && !wcscmp(argv[1],L"--add-tab-redraw")) {NotesAddTabRedraw();DestroyWindow(g_hGui);NotesBackgroundAudit("add-tab redraw cleanup");NotesBackgroundBanner();std::cout<<"Notes add-tab redraw: "<<checks<<" checks passed.\n";return 0;}
    if(argc>1 && !wcscmp(argv[1],L"--redraw-opening")) {NotesRedrawOpening();DestroyWindow(g_hGui);NotesBackgroundAudit("redraw/opening cleanup");NotesBackgroundBanner();std::cout<<"Notes redraw/opening: "<<checks<<" checks passed.\n";return 0;}
    if(argc>1 && !wcscmp(argv[1],L"--background-guard")) {NotesBackgroundGuardSelfTest();DestroyWindow(g_hGui);NotesBackgroundAudit("guard cleanup");NotesBackgroundBanner();std::cout<<"Notes background guard: "<<checks<<" checks passed.\n";return 0;}
    if(argc>1 && !wcscmp(argv[1],L"--workflow-background")) {NotesBackgroundWorkflows();DestroyWindow(g_hGui);NotesBackgroundAudit("workflow cleanup");NotesBackgroundBanner();std::cout<<"Notes background workflow: "<<checks<<" checks passed.\n";return 0;}
    if(argc>1 && !wcscmp(argv[1],L"--productivity-round4")) {NotesProductivityRound4(argc>2 && !wcscmp(argv[2],L"templates"));DestroyWindow(g_hGui);NotesBackgroundAudit("productivity round4 final");NotesBackgroundBanner();std::cout<<"Notes productivity round4: "<<checks<<" checks passed.\n";return 0;}
    if(argc>1 && !wcscmp(argv[1],L"--persistence-round4")) {NotesPersistenceRound4(argc>2 && !wcscmp(argv[2],L"pid-collision"));DestroyWindow(g_hGui);NotesBackgroundAudit("round4 persistence cleanup");NotesBackgroundBanner();std::cout<<"Notes persistence round 4: "<<checks<<" checks passed.\n";return 0;}
    if(argc>1 && !wcscmp(argv[1],L"--geometry-round3")) {const HWND foreground=GetForegroundWindow();NotesGeometryRound3(argc>2 ? argv[2] : L"");DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);Check(foregroundPid!=GetCurrentProcessId(),"Background geometry does not leave a test window in the foreground");DestroyWindow(g_hGui);NotesBackgroundAudit("geometry round3 final");NotesBackgroundBanner();std::cout<<"Suppressed UI: "<<suppressedVisibility<<" visibility, "<<suppressedFocus<<" focus calls; visibility/focus excluded. External foreground changed="<<(GetForegroundWindow()!=foreground)<<".\nNotes geometry round 3: "<<checks<<" checks passed.\n";return 0;}
    if(argc>1 && !wcscmp(argv[1],L"--persistence-round3")) {const HWND foreground=GetForegroundWindow();NotesPersistenceRound3();NotesRollbackTransactions();NotesCodeRollbackTransactions();DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);Check(foregroundPid!=GetCurrentProcessId(),"Background persistence does not leave a test window in the foreground");DestroyWindow(g_hGui);NotesBackgroundAudit("persistence round3 cleanup");NotesBackgroundBanner();std::cout<<"Suppressed UI: "<<suppressedVisibility<<" visibility, "<<suppressedFocus<<" focus, "<<suppressedProgress<<" progress calls; visibility/focus behavior excluded. External foreground changed="<<(GetForegroundWindow()!=foreground)<<".\nNotes persistence round 3: "<<checks<<" checks passed.\n";return 0;}
    if(argc>1 && !wcscmp(argv[1],L"--backup-snapshot")) {
      const HWND foreground=GetForegroundWindow();NotesAuditBackupSnapshot();DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);Check(foregroundPid!=GetCurrentProcessId(),"Background backup does not leave a test window in the foreground");DestroyWindow(g_hGui);NotesBackgroundAudit("backup cleanup");NotesBackgroundBanner();std::cout<<"Suppressed UI: "<<suppressedVisibility<<" visibility, "<<suppressedFocus<<" focus, "<<suppressedProgress<<" progress calls; visibility/focus behavior excluded. External foreground changed="<<(GetForegroundWindow()!=foreground)<<".\nNotes backup snapshot: "<<checks<<" checks passed.\n";return 0;
    }
    if(argc>1 && !wcscmp(argv[1],L"--audit-fixes")) {
      NotesAuditFailureRegressions();NotesAuditLibraryAndLinks();NotesAuditInactiveDetails();NotesAuditEditorCache(false);
      DestroyWindow(g_hGui);std::cout<<"Notes audit fixes: "<<checks<<" checks passed.\n";return 0;
    }
    if(argc>1 && !wcscmp(argv[1],L"--link-undo-binding")) {
      NotesLinkUndoBindings();DestroyWindow(g_hGui);NotesBackgroundAudit("link binding cleanup");NotesBackgroundBanner();std::cout<<"Notes link Undo binding: "<<checks<<" checks passed.\n";return 0;
    }
    if(argc>1 && !wcscmp(argv[1],L"--bulk-link-undo")) {NotesBulkLinkUndoBindings();DestroyWindow(g_hGui);std::cout<<"Notes bulk link Undo: "<<checks<<" checks passed.\n";return 0;}
    if(argc>1 && !wcscmp(argv[1],L"--extended-regression")) {HWND dialog=Open(L"Workspace QA");Tests(dialog);Extended(dialog);DestroyWindow(dialog);DestroyWindow(g_hGui);std::cout<<"Notes extended regression: "<<checks<<" checks passed.\n";return 0;}
    if(argc>1 && !wcscmp(argv[1],L"--snippet-link-limit")) {NotesSnippetLinkLimits(argc>2 && !wcscmp(argv[2],L"ranges"));DestroyWindow(g_hGui);std::cout<<"Notes snippet link limits: "<<checks<<" checks passed.\n";return 0;}
    if(argc>1 && !wcscmp(argv[1],L"--editor-resource-stress")) {
      NotesAuditEditorCache(true);DestroyWindow(g_hGui);std::cout<<"Notes 10,000-note resource stress: "<<checks<<" checks passed.\n";return 0;
    }
    if(argc>1 && !wcscmp(argv[1],L"--code-border")) {CodeBorderRegression();DestroyWindow(g_hGui);return 0;}
    if(argc>1 && !wcscmp(argv[1],L"--close-race")) {CloseAllLaunchRace();DestroyWindow(g_hGui);return 0;}
    if(argc>1 && !wcscmp(argv[1],L"--header-polish")) {NotesHeaderPolish();NotesClientSwitchPlacement();DestroyWindow(g_hGui);std::cout << "Notes header polish: " << checks << " checks passed.\n";return 0;}
    if(argc>1 && !wcscmp(argv[1],L"--status-indicator")) {
      NotesWorkStatusIndicator();CompactNotesActionRow();NotesTicketHeaderSizing();InspectorNavigation();NotesHeaderPolish();
      DestroyWindow(g_hGui);std::cout << "Notes status indicator: " << checks << " checks passed.\n";return 0;
    }
    if(argc>1 && !wcscmp(argv[1],L"--ticket-cleanup")) {
      NotesTicketHeaderActions();NotesWorkStatusIndicator();CompactNotesActionRow();NotesTicketHeaderSizing();NotesLinkBrowserPreference();
      InspectorNavigation();NotesHeaderPolish();NotesResizeWrapping();
      DestroyWindow(g_hGui);std::cout << "Notes ticket and title cleanup: " << checks << " checks passed.\n";return 0;
    }
    if(argc>1 && !wcscmp(argv[1],L"--links-details")) {
      NotesLinkBrowserDetails();NotesLinkBrowserPreference();InspectorNavigation();
      DestroyWindow(g_hGui);std::cout << "Notes visible browser choice: " << checks << " checks passed.\n";return 0;
    }
    if(argc>1 && !wcscmp(argv[1],L"--launcher-style")) {
      NotesLauncherStyle();CompactNotesActionRow();NotesHeaderPolish();NotesFormattingOnDemand();NotesResizeWrapping();NotesClientSwitchPlacement();
      DestroyWindow(g_hGui);std::cout << "Notes launcher style: " << checks << " checks passed.\n";return 0;
    }
    if(argc>1 && !wcscmp(argv[1],L"--button-hover")) {
      NotesButtonHover();
      DestroyWindow(g_hGui);std::cout << "Notes button hover: " << checks << " checks passed.\n";return 0;
    }
    if(argc>1 && !wcscmp(argv[1],L"--tab-overflow")) {
      NotesTabOverflowTransition(argc>2 ? fs::absolute(argv[2]) : fs::path{});
      NotesTabOrdering();NotesHeaderPolish();
      HWND owner=Open(L"Workspace QA");ResizeAndOverflow(owner);DestroyWindow(owner);
      NotesClientSwitchPlacement();
      DestroyWindow(g_hGui);std::cout << "Notes tab overflow: " << checks << " checks passed.\n";return 0;
    }
    if(argc>1 && !wcscmp(argv[1],L"--links-details-capture")) {
      CaptureNotesLinkBrowserDetails(argc>2 ? fs::absolute(argv[2]) : fs::current_path()/L"build/notes-browser-details/visual");DestroyWindow(g_hGui);return 0;
    }
    if(argc>1 && !wcscmp(argv[1],L"--links-ticket")) {
      NotesLinkBrowserPreference();NotesTicketHeaderSizing();CompactNotesActionRow();NotesFormattingOnDemand();
      HWND owner=Open(L"Workspace QA");ShowWindow(owner,SW_SHOW);SetFocus(Edit(owner));NotesLinksAndContextMenu(owner);TicketCodeAndTopActions(owner);DestroyWindow(owner);
      DestroyWindow(g_hGui);std::cout << "Notes browser and ticket header: " << checks << " checks passed.\n";return 0;
    }
    if(argc>1 && !wcscmp(argv[1],L"--action-row")) {
      CompactNotesActionRow();NotesFormattingOnDemand();HWND owner=Open(L"Workspace QA");TicketCodeAndTopActions(owner);ResizeAndOverflow(owner);DestroyWindow(owner);
      NotesResizeWrapping();DestroyWindow(g_hGui);std::cout << "Notes action row: " << checks << " checks passed.\n";return 0;
    }
    if(argc>1 && !wcscmp(argv[1],L"--action-row-capture")) {CaptureCompactNotesActionRow(argc>2 ? fs::absolute(argv[2]) : fs::current_path()/L"build/notes-action-row");DestroyWindow(g_hGui);return 0;}
    NotesPersistenceRound3();NotesRollbackTransactions();NotesCodeRollbackTransactions();NotesLinkUndoBindings();NotesSnippetLinkLimits();NotesSnippetLinkLimits(true);NotesLibrarySearchDecoder();NotesAuditBackupSnapshot();NotesAuditFailureRegressions();NotesAuditLibraryAndLinks();NotesAuditInactiveDetails();NotesAuditEditorCache(false);
    NotesAddTabRedraw();NotesLibraryResizeBorders();NotesTabTooltipLifetime();NotesTabOverflowTransition();NotesButtonHover();NotesLauncherStyle();
    HWND dialog=Open(L"Workspace QA"); Tests(dialog); Extended(dialog); ResizeAndOverflow(dialog); FrameRegression(dialog); PickerAndTemplates(dialog); Walkthrough(dialog); GuidanceAndBulkTrash(dialog); Productivity(dialog); TicketCodeAndTopActions(dialog); AddDeleteTransitions(dialog); DirectTemplateEditing(dialog); DirectTrashAndCompactHelp(dialog); NotesLinksAndContextMenu(dialog); DestroyWindow(dialog);
    dialog=Open(L"Workspace QA");
    Check(!HasActiveNote(State(dialog)),"Zero open tabs persists across restart");
    Check((GetWindowLongPtrW(Edit(dialog),GWL_STYLE)&WS_VISIBLE)==0,"Restart empty state does not expose blank editor");
    Command(dialog,IDC_NOTES_ADD_TAB); Check(HasActiveNote(State(dialog)),"New note from empty state");
    DestroyWindow(dialog);EmptyDraftRestart();CompactNormalSpacing();OldNotesSpacingUpgrade();DirectSnippetEditing();InspectorNavigation();NotesResizeWrapping();NotesTabOrdering();LauncherTrayTests();CloseAllLaunchRace();CodeBorderRegression();NotesHeaderPolish();NotesClientSwitchPlacement();CompactNotesActionRow();NotesFormattingOnDemand();NotesLinkBrowserDetails();NotesLinkBrowserPreference();NotesTicketHeaderSizing();NotesWorkStatusIndicator();NotesTicketHeaderActions();NotesPersistenceRound4();NotesPersistenceRound4(true);NotesProductivityRound4();DestroyWindow(g_hGui);
    std::cout << "Notes workspace: " << checks << " checks passed.\n"; return 0;
  } catch (const std::exception &error) { std::cerr << "FAIL: " << error.what() << std::endl; if(backgroundNotesTests) NotesBackgroundBanner();return 1; }
}
