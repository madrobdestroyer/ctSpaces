static unsigned chromeNativeShows=0,chromeNativeForegrounds=0,chromeNativeFocus=0;
static void CALLBACK ChromeNativeEvent(HWINEVENTHOOK,DWORD event,HWND window,LONG object,LONG,DWORD,DWORD) {
  if(!window) return;
  DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid && pid!=GetCurrentProcessId()) return;
  if(event==EVENT_SYSTEM_FOREGROUND) ++chromeNativeForegrounds;
  else if(event==EVENT_OBJECT_FOCUS) ++chromeNativeFocus;
  else if(event==EVENT_OBJECT_SHOW && object==OBJID_WINDOW) ++chromeNativeShows;
}
struct ChromeNativeObserver {
  std::vector<HWINEVENTHOOK> hooks;
  ChromeNativeObserver() {
    for(const DWORD event:{EVENT_SYSTEM_FOREGROUND,EVENT_OBJECT_SHOW,EVENT_OBJECT_FOCUS}) {
      const auto hook=SetWinEventHook(event,event,nullptr,ChromeNativeEvent,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);
      if(!hook) throw std::runtime_error("Cannot observe hidden main-chrome test windows");hooks.push_back(hook);
    }
  }
  ~ChromeNativeObserver(){for(const auto hook:hooks) UnhookWinEvent(hook);}
  void Audit() {
    MSG message{};unsigned count=0;
    while(count++<256 && PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
      ChromeCheck(!((message.message>=WM_KEYFIRST && message.message<=WM_KEYLAST) || (message.message>=WM_MOUSEFIRST && message.message<=WM_MOUSELAST)),
          "Hidden main-chrome tests receive no device keyboard or mouse messages");
      if(message.message!=WM_QUIT) DispatchMessageW(&message);
    }
    unsigned visible=0;
    EnumWindows([](HWND window,LPARAM data)->BOOL {DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid==GetCurrentProcessId() && IsWindowVisible(window)) ++*reinterpret_cast<unsigned *>(data);return TRUE;},reinterpret_cast<LPARAM>(&visible));
    DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
    GUITHREADINFO thread{sizeof(thread)};const bool read=GetGUIThreadInfo(GetCurrentThreadId(),&thread)!=FALSE;
    ChromeCheck(visible==0 && foregroundPid!=GetCurrentProcessId() && (!read || (!thread.hwndActive && !thread.hwndFocus && !thread.hwndCapture)),
        "Main-chrome tests leave zero visible windows, activation, focus, or capture");
    ChromeCheck(chromeNativeShows==0 && chromeNativeForegrounds==0 && chromeNativeFocus==0,
        "Native event observer records zero show, foreground, and focus events");
    std::cout << "Main chrome background audit: visible=" << visible << "; show/foreground/focus=" << chromeNativeShows << "/" << chromeNativeForegrounds << "/" << chromeNativeFocus << std::endl;
  }
};

struct ChromeDib {
  HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;DWORD *pixels=nullptr;int width=0,height=0;
  explicit ChromeDib(HWND window) {
    RECT area{};GetClientRect(window,&area);width=area.right;height=area.bottom;
    HDC native=GetDC(window);dc=CreateCompatibleDC(native);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    bitmap=CreateDIBSection(native,&info,DIB_RGB_COLORS,reinterpret_cast<void **>(&pixels),nullptr,0);ReleaseDC(window,native);
    if(!dc || !bitmap || !pixels) throw std::runtime_error("Cannot create offscreen main-chrome pixel fixture");previous=SelectObject(dc,bitmap);
  }
  ~ChromeDib(){if(previous) SelectObject(dc,previous);if(bitmap) DeleteObject(bitmap);if(dc) DeleteDC(dc);}
  std::vector<DWORD> Snapshot(){GdiFlush();std::vector<DWORD> result(pixels,pixels+width*height);for(auto &pixel:result) pixel&=0xffffff;return result;}
  void Clear(DWORD color){GdiFlush();std::fill(pixels,pixels+width*height,color);}
};

static void TestMainRepaint(const fs::path &captures) {
  // All paths belong to this disposable fixture, never installed client data.
  const auto savedData=g_sDataDir;
  g_sDataDir=fs::absolute(captures.empty() ? fs::temp_directory_path()/std::format(L"ctSpaces-main-paint-{}-{}",GetCurrentProcessId(),GetTickCount64()) : captures/L"main-paint-fixture");
  for(const wchar_t *name:{L"Paint A",L"Paint B",L"Paint C",L"Paint D",L"Paint E"}) fs::create_directories(g_sDataDir/L"Sites"/name);
  g_uiDpi=96;g_rcSessionTabs={0,0,600,34};g_rcPinnedArea={14,38,586,77};
  g_rcUtilityBar={0,140,600,180};g_rcBrowserSelector={14,145,168,175};
  RECT browserPaintBounds=g_rcBrowserSelector;InflateRect(&browserPaintBounds,1,1);
  g_pinnedClients={L"Paint A",L"Paint B",L"Paint C",L"Paint D",L"Paint E"};
  BuildPinnedClientRects();g_iHotPinnedClient=-1;g_bHotPinnedOverflow=false;g_bHotBrowserSelector=false;
  const auto center=[](const RECT &area){return POINT{(area.left+area.right)/2,(area.top+area.bottom)/2};};
  const auto move=[](POINT point){WndProc(g_hGui,WM_MOUSEMOVE,0,MAKELPARAM(point.x,point.y));};
  const auto clearInvalidations=[](){chromeInvalidations.clear();ValidateRect(g_hGui,nullptr);};
  const auto confinedTo=[](const std::vector<RECT> &allowed) {
    for(const auto &change:chromeInvalidations) {
      if(change.window!=g_hGui || change.erase) return false;
      bool matched=false;for(const auto &area:allowed) if(EqualRect(&area,&change.bounds)) matched=true;
      if(!matched) return false;
    }
    return true;
  };
  chromeRecordInvalidations=true;clearInvalidations();move(center(g_pinnedClientRects[0]));
  ChromeCheck(!chromeInvalidations.empty() && confinedTo({g_pinnedClientRects[0]}),
      "Entering a pinned client invalidates only that item without erasing the footer or other pins");
  clearInvalidations();move(center(g_pinnedClientRects[0]));
  ChromeCheck(chromeInvalidations.empty(),"Moving within the same pinned client schedules no repaint");
  clearInvalidations();move(center(g_pinnedClientRects[1]));
  ChromeCheck(chromeInvalidations.size()==2 && confinedTo({g_pinnedClientRects[0],g_pinnedClientRects[1]}),
      "Moving between pinned clients invalidates only the previous and new hot items");
  clearInvalidations();move(center(g_rcPinnedOverflow));
  ChromeCheck(chromeInvalidations.size()==2 && confinedTo({g_pinnedClientRects[1],g_rcPinnedOverflow}),
      "Pinned overflow hover invalidates only the former item and overflow");
  clearInvalidations();move(center(g_rcBrowserSelector));
  ChromeCheck(chromeInvalidations.size()==2 && confinedTo({g_rcPinnedOverflow,browserPaintBounds}),
      "Moving to the browser selector invalidates its antialiased border footprint and the former overflow");
  clearInvalidations();WndProc(g_hGui,WM_MOUSELEAVE,0,0);
  ChromeCheck(chromeInvalidations.size()==1 && confinedTo({browserPaintBounds}),
      "Mouse leave clears the complete antialiased browser selector footprint without erasing any band");
  clearInvalidations();WndProc(g_hGui,WM_MOUSELEAVE,0,0);
  ChromeCheck(chromeInvalidations.empty(),"Repeated mouse leave schedules no repaint");
  chromeRecordInvalidations=false;

  for(const wchar_t *themeName:{L"Dark - Gothic",L"Marine"}) {
    const auto theme=std::find_if(g_aCustomThemes.begin(),g_aCustomThemes.end(),[&](const ThemeEntry &entry){return entry.name==themeName;});
    g_themeColors=theme->colors;g_bThemeIsDark=theme->dark;UpdateThemeBrushes();
    ChromeDib surface(g_hGui);surface.Clear(0x1493e2);const auto beforeErase=surface.Snapshot();
    chromePaintTarget=surface.dc;chromeTargetFills=0;chromePresentations=0;
    ChromeCheck(WndProc(g_hGui,WM_ERASEBKGND,reinterpret_cast<WPARAM>(surface.dc),0)==1 && surface.Snapshot()==beforeErase,
        "Main background erase acknowledges painting without exposing a blank intermediate frame");
    WndProc(g_hGui,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(surface.dc),PRF_CLIENT);
    const auto normal=surface.Snapshot();
    ChromeCheck(normal!=beforeErase && chromeTargetFills==0 && chromePresentations==1,
        "Main client background and decorations reach the target in one buffered presentation");
    move(center(g_pinnedClientRects[0]));WndProc(g_hGui,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(surface.dc),PRF_CLIENT);
    ChromeCheck(surface.Snapshot()!=normal,"The complete production parent renderer shows pinned hover in both themes");
    WndProc(g_hGui,WM_MOUSELEAVE,0,0);WndProc(g_hGui,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(surface.dc),PRF_CLIENT);
    ChromeCheck(surface.Snapshot()==normal,"Leaving a pinned item restores the exact complete parent image");

    // Apply precisely the production invalidation rectangles to the old image,
    // then compare every pixel with a fresh complete render of the new state.
    chromeRecordInvalidations=true;
    for(const auto point:{center(g_pinnedClientRects[0]),center(g_pinnedClientRects[1]),center(g_rcPinnedOverflow),center(g_rcBrowserSelector),POINT{590,100}}) {
      clearInvalidations();move(point);
      HRGN dirty=CreateRectRgn(0,0,0,0);
      for(const auto &change:chromeInvalidations) {HRGN item=CreateRectRgnIndirect(&change.bounds);CombineRgn(dirty,dirty,item,RGN_OR);DeleteObject(item);}
      const int saved=SaveDC(surface.dc);SelectClipRgn(surface.dc,dirty);
      WndProc(g_hGui,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(surface.dc),PRF_CLIENT);RestoreDC(surface.dc,saved);DeleteObject(dirty);
      ChromeDib complete(g_hGui);WndProc(g_hGui,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(complete.dc),PRF_CLIENT);
      ChromeCheck(surface.Snapshot()==complete.Snapshot(),"Exact hover dirty rectangles reproduce the complete parent image including adjacent item dividers");
    }
    chromeRecordInvalidations=false;
    surface.Clear(0x1493e2);const int saved=SaveDC(surface.dc);IntersectClipRect(surface.dc,g_rcBrowserSelector.left,g_rcBrowserSelector.top,g_rcBrowserSelector.right,g_rcBrowserSelector.bottom);
    {std::lock_guard<std::mutex> lock(g_iconCacheMutex);g_iconCache.clear();}
    WndProc(g_hGui,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(surface.dc),PRF_CLIENT);RestoreDC(surface.dc,saved);
    const auto clipped=surface.Snapshot();
    ChromeCheck(g_iconCache.empty(),"A browser-only repaint does not resolve pinned client icons or touch their cache");
    bool untouched=true;for(int y=0;y<surface.height;++y) for(int x=0;x<surface.width;++x) if(!PtInRect(&g_rcBrowserSelector,POINT{x,y}) && clipped[y*surface.width+x]!=0x1493e2) untouched=false;
    ChromeCheck(untouched,"Buffered presentation retains the supplied dirty/child clipping outside the browser selector");
    for(const int failure:{0,1,2,3}) {
      const auto failNext=[&](){chromeFailPaintDC=failure==0;chromeFailPaintBitmap=failure==1;chromeFailPaintSelect=failure==2;chromeFailPaintPresent=failure==3;};
      const auto clearFailure=[](){chromeFailPaintDC=false;chromeFailPaintBitmap=false;chromeFailPaintSelect=false;chromeFailPaintPresent=false;};
      surface.Clear(0x1493e2);failNext();
      WndProc(g_hGui,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(surface.dc),PRF_CLIENT);
      clearFailure();
      ChromeCheck(surface.Snapshot()==normal,"Buffer creation, selection, or presentation failure falls back to a complete correctly themed parent paint");
      surface.Clear(0x1493e2);const int clippedSave=SaveDC(surface.dc);
      IntersectClipRect(surface.dc,g_rcBrowserSelector.left,g_rcBrowserSelector.top,g_rcBrowserSelector.right,g_rcBrowserSelector.bottom);failNext();
      WndProc(g_hGui,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(surface.dc),PRF_CLIENT);
      clearFailure();RestoreDC(surface.dc,clippedSave);
      ChromeCheck(surface.Snapshot()==clipped,"Every buffer failure fallback retains the exact supplied dirty and child clipping");
    }
    clearInvalidations();chromeRecordInvalidations=true;
    for(unsigned repeat=0;repeat<32;++repeat) {
      WndProc(g_hGui,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(surface.dc),PRF_CLIENT);
      ::InvalidateRect(g_hGui,&g_rcPinnedArea,FALSE);
      WndProc(g_hGui,WM_PAINT,0,0);
    }
    ChromeCheck(chromeInvalidations.empty() && !GetUpdateRect(g_hGui,nullptr,FALSE),
        "Repeated production print and paint validates its update region without scheduling a repaint feedback loop");
    chromeRecordInvalidations=false;
    chromePaintTarget=nullptr;
  }
  g_sDataDir=savedData;g_pinnedClients.clear();g_pinnedClientRects.clear();
}
