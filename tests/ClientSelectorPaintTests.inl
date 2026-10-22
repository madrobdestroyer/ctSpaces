// The normal Notes opening path also takes focus away from the launcher's
// client edit. Exercise that native message/notification path independently
// of ShowWindow or SetFocus, using only hidden process-owned controls.
static unsigned selectorFocusNotifications=0,selectorComboFocusNotifications=0,selectorNativeDraws=0;
static bool selectorKeyboardDropped=false;
static unsigned selectorKeyboardDropRequests=0;
static LRESULT CALLBACK SelectorKeyboardComboProc(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
  // Observe the actual edit-to-combo messages without creating a native popup.
  if(message==CB_GETDROPPEDSTATE) return selectorKeyboardDropped;
  if(message==CB_SHOWDROPDOWN) {
    selectorKeyboardDropped=w!=0;
    ++selectorKeyboardDropRequests;
    return TRUE;
  }
  return DefSubclassProc(window,message,w,l);
}
static LRESULT CALLBACK SelectorPaintParentProc(HWND window,UINT message,WPARAM w,LPARAM l) {
  if(message==WM_COMMAND && LOWORD(w)==IDC_CLIENT_EDIT && HIWORD(w)==EN_KILLFOCUS) {
    ++selectorFocusNotifications;
    return WndProc(window,message,w,l);
  }
  if(message==WM_COMMAND && LOWORD(w)==102 && HIWORD(w)==CBN_KILLFOCUS) {
    ++selectorComboFocusNotifications;
    return WndProc(window,message,w,l);
  }
  if(message==WM_DRAWITEM) {++selectorNativeDraws;return WndProc(window,message,w,l);}
  if(message==WM_CTLCOLORSTATIC || message==WM_CTLCOLOREDIT || message==WM_CTLCOLORBTN)
    return WndProc(window,message,w,l);
  // Do not run the launcher's WM_DESTROY global teardown for this fixture.
  return DefWindowProcW(window,message,w,l);
}

struct SelectorPaintFixture {
  HWND launcher=g_hGui,combo=g_hComboClient,edit=g_hClientEdit,surface=g_hClientEditSurface;
  HWND pin=g_hBtnPin,restore=g_hBtnRestoreTabs,icon=g_hBtnClientIcon,drop=g_hBtnClientDrop,go=g_hBtnGo;
  HWND pinTip=g_hBtnPinTip;
  HICON preview=g_hIconPreviewHandle;
  fs::path data=g_sDataDir,config=g_sConfigPath;
  std::vector<std::wstring> names=g_allClientNames;
  int selected=g_iSelectedSessionTab;
  bool enabled=g_bUiEnabled;
  HWND owner=nullptr;
  explicit SelectorPaintFixture(const fs::path &captures) {
    g_sDataDir=fs::absolute(captures.empty() ?
        fs::temp_directory_path()/std::format(L"ctSpaces-selector-paint-{}-{}",GetCurrentProcessId(),GetTickCount64()) :
        captures/L"selector-paint-fixture");
    fs::create_directories(g_sDataDir/L"Sites"/L"Selector QA");
    g_sConfigPath=g_sDataDir/L"config.ini";
    g_allClientNames={L"Selector QA"};g_iSelectedSessionTab=0;g_bUiEnabled=true;
    g_hIconPreviewHandle=nullptr;g_hBtnPinTip=nullptr;
    WNDCLASSW type{};type.hInstance=g_hInst;type.lpszClassName=L"CtSpacesHiddenSelectorPaintFixture";
    type.lpfnWndProc=SelectorPaintParentProc;
    if(!RegisterClassW(&type) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
      throw std::runtime_error("Cannot register hidden selector paint fixture");
    owner=CreateWindowExW(WS_EX_NOACTIVATE,type.lpszClassName,L"Hidden selector paint fixture",
        WS_OVERLAPPED|WS_CLIPCHILDREN,0,0,600,210,nullptr,nullptr,g_hInst,nullptr);
    g_hGui=owner;
    if(!owner) throw std::runtime_error("Cannot create hidden selector parent");
    const auto child=[&](const wchar_t *kind,const wchar_t *label,DWORD style,int id,int x,int y,int width,int height) {
      HWND control=CreateWindowExW(WS_EX_NOACTIVATE,kind,label,WS_CHILD|WS_CLIPSIBLINGS|style,
          x,y,width,height,owner,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),g_hInst,nullptr);
      if(!control) throw std::runtime_error("Cannot create hidden selector child");
      return control;
    };
    // Every reachable launcher handle is process-owned and non-null. No API
    // call is permitted to use NULL as an accidental desktop redraw target.
    g_hComboClient=child(L"COMBOBOX",L"",CBS_DROPDOWN|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,102,14,85,350,120);
    g_hClientEditSurface=child(L"STATIC",L"",SS_OWNERDRAW|SS_NOTIFY,IDC_CLIENT_EDIT_SURFACE,14,85,350,36);
    g_hClientEdit=child(L"EDIT",L"Selector QA",ES_AUTOHSCROLL,IDC_CLIENT_EDIT,56,94,260,20);
    g_hBtnPin=child(L"BUTTON",L"",BS_OWNERDRAW,IDC_BTN_PIN,374,85,36,36);
    g_hBtnRestoreTabs=child(L"BUTTON",L"Restore tabs",BS_OWNERDRAW,IDC_BTN_RESTORE_TABS,300,155,132,30);
    g_hBtnClientIcon=child(L"BUTTON",L"Open profile folder",BS_OWNERDRAW,IDC_BTN_CLIENT_ICON,14,85,34,36);
    g_hBtnClientDrop=child(L"BUTTON",L"Show client list",BS_OWNERDRAW,IDC_BTN_CLIENT_DROP,330,85,34,36);
    g_hBtnGo=child(L"BUTTON",L"Open",BS_OWNERDRAW,IDOK,420,85,78,36);
#if !defined(CTSPACES_SELECTOR_BEFORE)
    // This is the same setup installed by the repaired production launcher.
    // The before-fix binary intentionally has no such helper or installation.
    SetWindowSubclass(g_hClientEditSurface,ClientEditSurfaceSubclassProc,1,0);
#endif
    SetWindowSubclass(g_hClientEdit,ClientEditSubclassProc,1,0);
    SetWindowSubclass(g_hComboClient,ComboThemeSubclassProc,1,0);
    SendMessageW(g_hComboClient,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Selector QA"));
    HideClientComboChrome(g_hComboClient);
    LayoutClientComboChildren(g_hComboClient);
    for(const HWND control:{owner,g_hComboClient,g_hClientEditSurface,g_hClientEdit,g_hBtnPin,
        g_hBtnRestoreTabs,g_hBtnClientIcon,g_hBtnClientDrop,g_hBtnGo}) {
      DWORD pid=0;GetWindowThreadProcessId(control,&pid);
      ChromeCheck(pid==GetCurrentProcessId() && !IsWindowVisible(control),
          "Every normal-opening selector fixture handle is owned by this hidden process");
    }
  }
  ~SelectorPaintFixture() {
    chromeSelectorKeyTarget=nullptr;chromeSelectorAltDown=false;
    chromeRecordInvalidations=false;chromeRecordRedraws=false;chromePaintTarget=nullptr;
    chromeFailPaintDC=chromeFailPaintBitmap=chromeFailPaintSelect=chromeFailPaintPresent=false;
    if(owner) DestroyWindow(owner);
    if(g_hIconPreviewHandle && g_hIconPreviewHandle!=preview) DestroyIcon(g_hIconPreviewHandle);
    g_hGui=launcher;g_hComboClient=combo;g_hClientEdit=edit;g_hClientEditSurface=surface;
    g_hBtnPin=pin;g_hBtnRestoreTabs=restore;g_hBtnClientIcon=icon;g_hBtnClientDrop=drop;g_hBtnGo=go;
    g_hBtnPinTip=pinTip;g_hIconPreviewHandle=preview;
    g_sDataDir=data;g_sConfigPath=config;g_allClientNames=std::move(names);
    g_iSelectedSessionTab=selected;g_bUiEnabled=enabled;
  }
};

static void RunClientSelectorPaintTests(const fs::path &captures) {
  SelectorPaintFixture fixture(captures);
  {
    const bool navigated=g_bClientListNavigated,suppressed=g_bSuppressClientSelectionCommit;
    struct KeyboardProbeScope {
      HWND combo;
      bool navigated,suppressed;
      ~KeyboardProbeScope() {
        RemoveWindowSubclass(combo,SelectorKeyboardComboProc,3);
        chromeSelectorKeyTarget=nullptr;chromeSelectorAltDown=false;
        g_bClientListNavigated=navigated;g_bSuppressClientSelectionCommit=suppressed;
      }
    } keyboardProbe{g_hComboClient,navigated,suppressed};
    ChromeCheck(SetWindowSubclass(g_hComboClient,SelectorKeyboardComboProc,3,0)!=FALSE,
        "Selector keyboard probe intercepts dropdown requests before any native popup can open");
    chromeSelectorKeyTarget=g_hClientEdit;chromeSelectorAltDown=false;
    selectorKeyboardDropped=false;selectorKeyboardDropRequests=chromeSelectorKeyForwards=0;
    SendMessageW(g_hClientEdit,WM_KEYDOWN,VK_F4,1);
    ChromeCheck(selectorKeyboardDropped && selectorKeyboardDropRequests==1 && chromeSelectorKeyForwards==0,
        "Plain F4 opens the client dropdown through the real edit subclass");
    SendMessageW(g_hClientEdit,WM_KEYDOWN,VK_F4,1);
    ChromeCheck(!selectorKeyboardDropped && selectorKeyboardDropRequests==2,
        "Plain F4 closes the client dropdown through the real edit subclass");
    chromeSelectorAltDown=true;
    const LRESULT forwarded=SendMessageW(g_hClientEdit,WM_SYSKEYDOWN,VK_F4,1L|(1L<<29));
    ChromeCheck(forwarded==0x433 && chromeSelectorKeyForwards==1 && selectorKeyboardDropRequests==2 && !selectorKeyboardDropped,
        "Alt+F4 is forwarded to native window handling without opening the client dropdown");
    SendMessageW(g_hClientEdit,WM_SYSKEYDOWN,VK_DOWN,1L|(1L<<29));
    ChromeCheck(selectorKeyboardDropped && selectorKeyboardDropRequests==3,
        "Alt+Down still opens the client dropdown");
    SendMessageW(g_hClientEdit,WM_SYSKEYDOWN,VK_F4,1L|(1L<<29));
    ChromeCheck(chromeSelectorKeyForwards==2 && selectorKeyboardDropRequests==3 && selectorKeyboardDropped,
        "Alt+F4 is forwarded unchanged even when the client dropdown is already open");
    SendMessageW(g_hClientEdit,WM_SYSKEYDOWN,VK_DOWN,1L|(1L<<29));
    ChromeCheck(!selectorKeyboardDropped && selectorKeyboardDropRequests==4,
        "Alt+Down still closes the client dropdown without desktop input");
    SendMessageW(g_hClientEdit,WM_KEYDOWN,VK_F4,1);
    ChromeCheck(chromeSelectorKeyForwards==3 && selectorKeyboardDropRequests==4,
        "F4 with Alt held is never consumed as the plain dropdown shortcut");
  }
  const auto startInvalidations=[]() {
    chromeInvalidations.clear();chromeRedrawRequests.clear();
    chromeRecordInvalidations=true;chromeRecordRedraws=true;
  };
  const auto stopInvalidations=[](){chromeRecordInvalidations=false;chromeRecordRedraws=false;};
  const auto noEagerErase=[]() {
    return std::none_of(chromeRedrawRequests.begin(),chromeRedrawRequests.end(),[](const auto &call) {
      return (call.flags&(RDW_UPDATENOW|RDW_ERASE|RDW_ERASENOW))!=0;
    }) && std::none_of(chromeInvalidations.begin(),chromeInvalidations.end(),[](const auto &call){return call.erase!=FALSE;});
  };
  const auto paintNative=[](ChromeDib &target) {
    chromePaintTarget=target.dc;chromeTargetFills=chromePresentations=0;selectorNativeDraws=0;
    SendMessageW(g_hClientEditSurface,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(target.dc),PRF_CLIENT);
    GdiFlush();
  };
  // Record all three observed symptoms before asserting, so the preserved
  // before-fix log can distinguish direct drawing from eager focus repaints.
  ChromeDib probe(g_hClientEditSurface);probe.Clear(0x1493e2);paintNative(probe);
  const unsigned initialDraws=selectorNativeDraws,initialFills=chromeTargetFills,initialPresents=chromePresentations;
  selectorFocusNotifications=0;startInvalidations();
  SendMessageW(g_hClientEdit,WM_KILLFOCUS,0,0);
  // Send the native edit's corresponding parent notification as well. The
  // real desktop focus is intentionally never moved by these tests.
  SendMessageW(g_hGui,WM_COMMAND,MAKEWPARAM(IDC_CLIENT_EDIT,EN_KILLFOCUS),reinterpret_cast<LPARAM>(g_hClientEdit));
  stopInvalidations();
  const bool initialCoalesced=noEagerErase();
  std::cout << "Normal Notes activation selector probe: parent EN_KILLFOCUS=" << selectorFocusNotifications
      << "; native owner draws=" << initialDraws << "; target fills=" << initialFills
      << "; presentations=" << initialPresents << "; invalidations=" << chromeInvalidations.size()
      << "; redraws=" << chromeRedrawRequests.size() << "; erase/synchronous requests=" << (!initialCoalesced) << std::endl;
  ChromeCheck(selectorFocusNotifications>0,"Normal Notes activation traverses the actual launcher EN_KILLFOCUS route without changing desktop focus");
  ChromeCheck(initialCoalesced,"Normal Notes activation coalesces launcher selector repaints without erase or synchronous refresh requests");
  ChromeCheck(initialDraws==1 && initialFills==0 && initialPresents==1,
      "The native selector print path routes owner drawing through one complete buffered presentation");

  probe.Clear(0x1493e2);const auto beforeErase=probe.Snapshot();chromePaintTarget=probe.dc;
  ChromeCheck(SendMessageW(g_hClientEditSurface,WM_ERASEBKGND,reinterpret_cast<WPARAM>(probe.dc),0)==1 &&
      probe.Snapshot()==beforeErase,"The real selector background erase is acknowledged without exposing a cleared frame");

  for(const wchar_t *themeName:{L"Dark - Gothic",L"Marine"}) {
    const auto theme=std::find_if(g_aCustomThemes.begin(),g_aCustomThemes.end(),[&](const ThemeEntry &entry){return entry.name==themeName;});
    ChromeCheck(theme!=g_aCustomThemes.end(),"Selector activation checks use a real application dark or light theme");
    g_themeColors=theme->colors;g_bThemeIsDark=theme->dark;UpdateThemeBrushes();
    ChromeDib surface(g_hClientEditSurface);surface.Clear(0x1493e2);paintNative(surface);
    const auto normal=surface.Snapshot();
    ChromeCheck(selectorNativeDraws==1 && chromeTargetFills==0 && chromePresentations==1,
        "Native selector owner drawing presents its complete themed surface once");
    ChromeCheck(normal!=std::vector<DWORD>(normal.size(),0x1493e2),"Normal selector printing produces opaque themed pixels");
    const DWORD center=normal[(surface.height/2)*surface.width+surface.width/2];
    const DWORD expected=(GetRValue(g_themeColors.crControl)<<16)|(GetGValue(g_themeColors.crControl)<<8)|GetBValue(g_themeColors.crControl);
    ChromeCheck(center==expected,"The selector interior matches the selected theme rather than a system or blank background");
#if !defined(CTSPACES_SELECTOR_BEFORE)
    ChromeDib originalCore(g_hClientEditSurface);originalCore.Clear(0x1493e2);
    DRAWITEMSTRUCT raw{};raw.CtlType=ODT_STATIC;raw.CtlID=IDC_CLIENT_EDIT_SURFACE;
    raw.hwndItem=g_hClientEditSurface;raw.hDC=originalCore.dc;GetClientRect(g_hClientEditSurface,&raw.rcItem);
    DrawClientEditSurfaceCore(raw);
    ChromeCheck(originalCore.Snapshot()==normal,
        "The native buffered selector preserves every original core-renderer pixel in each theme");
#endif

    // The comparison reference is a complete production render; the clipped
    // render must preserve every preexisting pixel outside the dirty area.
    RECT dirty{surface.width/3,2,surface.width-3,surface.height-2};
    surface.Clear(0x1493e2);const int saved=SaveDC(surface.dc);IntersectClipRect(surface.dc,dirty.left,dirty.top,dirty.right,dirty.bottom);
    paintNative(surface);RestoreDC(surface.dc,saved);const auto clipped=surface.Snapshot();
    bool exact=true;for(int y=0;y<surface.height;++y) for(int x=0;x<surface.width;++x) {
      const size_t index=static_cast<size_t>(y)*surface.width+x;
      if(clipped[index]!=(PtInRect(&dirty,POINT{x,y}) ? normal[index] : 0x1493e2)) exact=false;
    }
    ChromeCheck(exact,"Selector presentation preserves exact dirty clipping and the untouched surrounding pixels");
#if !defined(CTSPACES_SELECTOR_BEFORE)
    originalCore.Clear(0x1493e2);const int rawSaved=SaveDC(originalCore.dc);
    IntersectClipRect(originalCore.dc,dirty.left,dirty.top,dirty.right,dirty.bottom);DrawClientEditSurfaceCore(raw);
    RestoreDC(originalCore.dc,rawSaved);
    ChromeCheck(originalCore.Snapshot()==clipped,
        "The native partial selector paint matches the original core pixels with identical dirty clipping");
#endif
    for(const int failure:{0,1,2,3}) {
      const auto setFailure=[&]() {chromeFailPaintDC=failure==0;chromeFailPaintBitmap=failure==1;chromeFailPaintSelect=failure==2;chromeFailPaintPresent=failure==3;};
      const auto clearFailure=[](){chromeFailPaintDC=chromeFailPaintBitmap=chromeFailPaintSelect=chromeFailPaintPresent=false;};
      surface.Clear(0x1493e2);setFailure();paintNative(surface);clearFailure();
      ChromeCheck(surface.Snapshot()==normal,"Selector buffer allocation, selection or presentation failure retains the complete final theme pixels");
      surface.Clear(0x1493e2);const int clipSave=SaveDC(surface.dc);IntersectClipRect(surface.dc,dirty.left,dirty.top,dirty.right,dirty.bottom);
      setFailure();paintNative(surface);clearFailure();RestoreDC(surface.dc,clipSave);
      ChromeCheck(surface.Snapshot()==clipped,"Selector resource-failure fallback retains exact partial-paint clipping");
    }
    startInvalidations();
    for(unsigned repeat=0;repeat<32;++repeat) {
      SendMessageW(g_hClientEdit,WM_KILLFOCUS,0,0);
      SendMessageW(g_hGui,WM_COMMAND,MAKEWPARAM(IDC_CLIENT_EDIT,EN_KILLFOCUS),reinterpret_cast<LPARAM>(g_hClientEdit));
    }
    stopInvalidations();
    ChromeCheck(noEagerErase(),"Repeated normal-opening focus notifications never schedule erase or synchronous launcher refreshes");
    selectorComboFocusNotifications=0;startInvalidations();
    for(unsigned repeat=0;repeat<32;++repeat) {
      SendMessageW(g_hComboClient,WM_KILLFOCUS,0,0);
      SendMessageW(g_hGui,WM_COMMAND,MAKEWPARAM(102,CBN_KILLFOCUS),reinterpret_cast<LPARAM>(g_hComboClient));
    }
    stopInvalidations();
    ChromeCheck(selectorComboFocusNotifications>=32,
        "Normal Notes activation traverses the native combo and actual launcher CBN_KILLFOCUS route without desktop focus");
    ChromeCheck(noEagerErase(),
        "Repeated combo focus loss coalesces selector repaints without erase or synchronous launcher refreshes");
    startInvalidations();for(unsigned repeat=0;repeat<32;++repeat) paintNative(surface);stopInvalidations();
    ChromeCheck(chromeInvalidations.empty() && chromeRedrawRequests.empty(),
        "Repeated native selector printing schedules no new painting or layout feedback");
    chromePaintTarget=nullptr;
  }
}
