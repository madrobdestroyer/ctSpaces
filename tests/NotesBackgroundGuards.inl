// Background scenarios use real native controls, but never display/activate them
// or touch the user's clipboard, keyboard state, capture, tray or browsers.
// Only the explicit link-context fixture enables this synthetic write target.
static bool notesCaptureClipboard=false;
static std::wstring notesCapturedClipboard;
static unsigned notesCapturedClipboardWrites=0;
struct NotesBackgroundGuardCounts {
  unsigned windowsCreated=0,modelessCreated=0,initialFocusSuppressed=0;
  unsigned creationVisibleRequested=0,creationVisibleSuppressed=0;
  unsigned showRequested=0,showSuppressed=0,positionShowRequested=0,positionShowSuppressed=0;
  unsigned focusRequested=0,focusSuppressed=0,foregroundRequested=0,foregroundSuppressed=0;
  unsigned activeSuppressed=0,raiseSuppressed=0,captureSuppressed=0,releaseSuppressed=0;
  unsigned progressRequested=0,progressSuppressed=0,modalSuppressed=0;
  unsigned forbiddenClipboard=0,forbiddenKeyboard=0,forbiddenRedraw=0,nativeVisibleEvents=0,nativeForegroundEvents=0,nativeFocusEvents=0;
} backgroundGuard;
static std::set<HWND> backgroundTopWindows;
static LRESULT CALLBACK NotesBackgroundRichGuard(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR) {
  if(backgroundNotesTests && (message==WM_COPY || message==WM_CUT || message==WM_PASTE)) {++backgroundGuard.forbiddenClipboard;return 0;}
  if(message==WM_NCDESTROY) RemoveWindowSubclass(window,NotesBackgroundRichGuard,id);
  return DefSubclassProc(window,message,w,l);
}
static bool NotesBackgroundSelector(const wchar_t *selector) {
  for(const auto *supported:{L"--persistence-round3",L"--persistence-round4",L"--productivity-round4",L"--backup-snapshot",L"--geometry-round3",L"--link-undo-binding",L"--bulk-link-undo",L"--snippet-link-limit",L"--background-guard",L"--workflow-background",L"--redraw-opening",L"--normal-opening",L"--library-resize-border",L"--tab-tooltip-lifetime",L"--add-tab-redraw",L"--plus-switch-background",L"--rollback-transactions-background",L"--code-rollback-background"})
    if(!wcscmp(selector,supported)) return true;
  return false;
}
static void CALLBACK NotesBackgroundEvent(HWINEVENTHOOK,DWORD event,HWND window,LONG object,LONG,DWORD,DWORD) {
  // Hooks are filtered to this PID. Retain already-destroyed owned windows so
  // a short transient is not discarded at callback delivery.
  DWORD pid=0;if(!window || (GetWindowThreadProcessId(window,&pid) && pid!=GetCurrentProcessId())) return;
  if(event==EVENT_SYSTEM_FOREGROUND) ++backgroundGuard.nativeForegroundEvents;
  else if(event==EVENT_OBJECT_FOCUS) ++backgroundGuard.nativeFocusEvents;
  else if(event==EVENT_OBJECT_SHOW && object==OBJID_WINDOW && (backgroundTopWindows.contains(window) || IsWindowVisible(GetAncestor(window,GA_ROOT)))) ++backgroundGuard.nativeVisibleEvents;
}
struct NotesBackgroundEvents {
  std::vector<HWINEVENTHOOK> hooks;
  void Start() {
    for(const DWORD event:{EVENT_SYSTEM_FOREGROUND,EVENT_OBJECT_SHOW,EVENT_OBJECT_FOCUS}) {
      const auto hook=SetWinEventHook(event,event,nullptr,NotesBackgroundEvent,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);
      if(!hook) throw std::runtime_error("Background native event observer could not be installed");hooks.push_back(hook);
    }
  }
  ~NotesBackgroundEvents(){for(const auto hook:hooks) UnhookWinEvent(hook);}
};
static void NotesBackgroundAudit(const char *scenario) {
  if(!backgroundNotesTests) return;
  MSG message{};unsigned pumped=0;
  while(pumped++<256 && PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
    if(message.message==WM_QUIT) continue;
    // All scenario actions are native data messages; device-input injection is
    // outside this scope, including process-local drag/keyboard simulation.
    if((message.message>=WM_KEYFIRST && message.message<=WM_KEYLAST) || (message.message>=WM_MOUSEFIRST && message.message<=WM_MOUSELAST)) {++backgroundGuard.forbiddenKeyboard;continue;}
    DispatchMessageW(&message);
  }
  unsigned visible=0;EnumWindows([](HWND window,LPARAM count)->BOOL {DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid==GetCurrentProcessId() && IsWindowVisible(window)) ++*reinterpret_cast<unsigned *>(count);return TRUE;},reinterpret_cast<LPARAM>(&visible));
  DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
  GUITHREADINFO thread{sizeof(thread)};const bool threadRead=GetGUIThreadInfo(GetCurrentThreadId(),&thread)!=FALSE;
  std::cout<<"Background scenario audit: "<<scenario<<"; visible owned top-level windows="<<visible<<"; native show/foreground/focus events="<<backgroundGuard.nativeVisibleEvents<<"/"<<backgroundGuard.nativeForegroundEvents<<"/"<<backgroundGuard.nativeFocusEvents<<std::endl;
  if(visible || foregroundPid==GetCurrentProcessId() || (threadRead && (thread.hwndActive || thread.hwndFocus || thread.hwndCapture)) || backgroundGuard.nativeVisibleEvents || backgroundGuard.nativeForegroundEvents || backgroundGuard.nativeFocusEvents || backgroundGuard.forbiddenClipboard || backgroundGuard.forbiddenKeyboard || backgroundGuard.forbiddenRedraw)
    throw std::runtime_error("Background scope attempted visibility, activation, input capture or user clipboard/keyboard access");
}
static void NotesBackgroundBanner() {
  const auto &c=backgroundGuard;
  std::cout<<"Background guards: {\"windowsCreated\":"<<c.windowsCreated<<",\"modelessCreated\":"<<c.modelessCreated<<",\"initialFocusSuppressed\":"<<c.initialFocusSuppressed
    <<",\"creationVisibleRequested\":"<<c.creationVisibleRequested<<",\"creationVisibleSuppressed\":"<<c.creationVisibleSuppressed
    <<",\"showRequested\":"<<c.showRequested<<",\"showSuppressed\":"<<c.showSuppressed<<",\"positionShowRequested\":"<<c.positionShowRequested<<",\"positionShowSuppressed\":"<<c.positionShowSuppressed
    <<",\"focusRequested\":"<<c.focusRequested<<",\"focusSuppressed\":"<<c.focusSuppressed<<",\"foregroundRequested\":"<<c.foregroundRequested<<",\"foregroundSuppressed\":"<<c.foregroundSuppressed
    <<",\"activeSuppressed\":"<<c.activeSuppressed<<",\"raiseSuppressed\":"<<c.raiseSuppressed<<",\"captureSuppressed\":"<<c.captureSuppressed<<",\"releaseSuppressed\":"<<c.releaseSuppressed
    <<",\"progressRequested\":"<<c.progressRequested<<",\"progressSuppressed\":"<<c.progressSuppressed<<",\"modalSuppressed\":"<<c.modalSuppressed
    <<",\"forbiddenClipboard\":"<<c.forbiddenClipboard<<",\"forbiddenKeyboard\":"<<c.forbiddenKeyboard<<",\"forbiddenRedraw\":"<<c.forbiddenRedraw<<",\"nativeVisibleEvents\":"<<c.nativeVisibleEvents<<",\"nativeForegroundEvents\":"<<c.nativeForegroundEvents<<",\"nativeFocusEvents\":"<<c.nativeFocusEvents<<"}"<<std::endl;
  std::cout<<"Background scope excludes visibility, focus, mouse/drag, device input, real clipboard, real tray and actual browser launches. User foreground changes are allowed.\n";
}
struct NotesHiddenDialogInit {DLGPROC procedure;LPARAM data;};
static constexpr const wchar_t *kNotesHiddenDialogProc=L"ctSpaces.notes.hidden-test-proc";
static INT_PTR CALLBACK NotesHiddenDialogProc(HWND window,UINT message,WPARAM w,LPARAM l) {
  if(message==WM_INITDIALOG) {
    const auto &init=*reinterpret_cast<NotesHiddenDialogInit *>(l);SetPropW(window,kNotesHiddenDialogProc,reinterpret_cast<HANDLE>(init.procedure));
    init.procedure(window,message,w,init.data);++backgroundGuard.initialFocusSuppressed;return FALSE;
  }
  const auto original=reinterpret_cast<DLGPROC>(GetPropW(window,kNotesHiddenDialogProc));
  const auto result=original ? original(window,message,w,l) : FALSE;
  if(message==WM_NCDESTROY) RemovePropW(window,kNotesHiddenDialogProc);
  return result;
}
HWND WINAPI notes_test::Modeless(HINSTANCE instance,LPCWSTR name,HWND owner,DLGPROC procedure,LPARAM data) {
  if(!backgroundNotesTests) return ::CreateDialogParamW(instance,name,owner,procedure,data);
  const auto resource=FindResourceW(instance,name,RT_DIALOG);const auto loaded=resource ? LoadResource(instance,resource) : nullptr;
  const auto bytes=resource ? SizeofResource(instance,resource) : 0;const auto source=loaded ? static_cast<const BYTE *>(LockResource(loaded)) : nullptr;
  if(!source || bytes<18) throw std::runtime_error("Hidden modeless dialog template unavailable");
  std::vector<BYTE> copy(source,source+bytes);WORD signature=0;memcpy(&signature,copy.data()+2,2);
  const size_t styleOffset=signature==0xffff ? 12 : 0,extendedOffset=signature==0xffff ? 8 : 4;
  DWORD style=0,extended=0;memcpy(&style,copy.data()+styleOffset,4);memcpy(&extended,copy.data()+extendedOffset,4);
  if(style&WS_VISIBLE){++backgroundGuard.creationVisibleRequested;++backgroundGuard.creationVisibleSuppressed;}style&=~WS_VISIBLE;extended|=WS_EX_NOACTIVATE;
  memcpy(copy.data()+styleOffset,&style,4);memcpy(copy.data()+extendedOffset,&extended,4);
  NotesHiddenDialogInit init{procedure,data};
  const auto window=::CreateDialogIndirectParamW(instance,reinterpret_cast<const DLGTEMPLATE *>(copy.data()),owner,NotesHiddenDialogProc,reinterpret_cast<LPARAM>(&init));
  if(window){++backgroundGuard.windowsCreated;++backgroundGuard.modelessCreated;}
  if(window && !(style&WS_CHILD)) backgroundTopWindows.insert(window);
  if(window) EnumChildWindows(window,[](HWND child,LPARAM)->BOOL {wchar_t type[32]{};GetClassNameW(child,type,32);if(!_wcsicmp(type,L"RICHEDIT50W")) SetWindowSubclass(child,NotesBackgroundRichGuard,0x4E424747,0);return TRUE;},0);
  return window;
}
HWND WINAPI notes_test::Active(HWND window){if(backgroundNotesTests){++backgroundGuard.activeSuppressed;return nullptr;}return ::SetActiveWindow(window);}
BOOL WINAPI notes_test::Raise(HWND window){if(backgroundNotesTests){++backgroundGuard.raiseSuppressed;return FALSE;}return ::BringWindowToTop(window);}
HWND WINAPI notes_test::Capture(HWND window){if(backgroundNotesTests){++backgroundGuard.captureSuppressed;return nullptr;}return ::SetCapture(window);}
BOOL WINAPI notes_test::Release(){if(backgroundNotesTests){++backgroundGuard.releaseSuppressed;return FALSE;}return ::ReleaseCapture();}
static void NotesBackgroundForbidClipboard(){if(backgroundNotesTests){++backgroundGuard.forbiddenClipboard;throw std::runtime_error("Real clipboard access excluded from background scope");}}
BOOL WINAPI notes_test::ClipboardOpen(HWND window){if(backgroundNotesTests&&notesCaptureClipboard)return TRUE;NotesBackgroundForbidClipboard();return ::OpenClipboard(window);}
BOOL WINAPI notes_test::ClipboardEmpty(){if(backgroundNotesTests&&notesCaptureClipboard){notesCapturedClipboard.clear();return TRUE;}NotesBackgroundForbidClipboard();return ::EmptyClipboard();}
BOOL WINAPI notes_test::ClipboardClose(){if(backgroundNotesTests&&notesCaptureClipboard)return TRUE;NotesBackgroundForbidClipboard();return ::CloseClipboard();}
HANDLE WINAPI notes_test::ClipboardSet(UINT format,HANDLE data){
  if(backgroundNotesTests&&notesCaptureClipboard){
    if(format!=CF_UNICODETEXT||!data)throw std::runtime_error("Unexpected synthetic clipboard format");
    const auto value=static_cast<const wchar_t *>(GlobalLock(data));const auto size=GlobalSize(data)/sizeof(wchar_t);
    if(!value||!size)throw std::runtime_error("Synthetic clipboard data unavailable");
    const auto end=std::find(value,value+size,L'\0');if(end==value+size){GlobalUnlock(data);throw std::runtime_error("Synthetic clipboard text is unterminated");}
    notesCapturedClipboard.assign(value,end);GlobalUnlock(data);GlobalFree(data);++notesCapturedClipboardWrites;return reinterpret_cast<HANDLE>(1);
  }
  NotesBackgroundForbidClipboard();return ::SetClipboardData(format,data);
}
HANDLE WINAPI notes_test::ClipboardGet(UINT format){NotesBackgroundForbidClipboard();return ::GetClipboardData(format);}
BOOL WINAPI notes_test::ClipboardAvailable(UINT format){if(backgroundNotesTests&&notesCaptureClipboard)return FALSE;NotesBackgroundForbidClipboard();return ::IsClipboardFormatAvailable(format);}
BOOL WINAPI notes_test::KeyboardState(const BYTE *state){if(backgroundNotesTests){++backgroundGuard.forbiddenKeyboard;throw std::runtime_error("Keyboard state changes excluded from background scope");}return ::SetKeyboardState(const_cast<BYTE *>(state));}
UINT WINAPI notes_test::Input(UINT count,LPINPUT input,int bytes){if(backgroundNotesTests){++backgroundGuard.forbiddenKeyboard;throw std::runtime_error("Device input injection excluded from background scope");}return ::SendInput(count,input,bytes);}
