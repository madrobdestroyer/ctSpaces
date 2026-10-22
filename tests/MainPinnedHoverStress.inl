// This fixture exercises local native message dispatch only. It never sends
// device input, shows a window, activates one, or reads the desktop pixels.
static unsigned chromeStressNativePaints=0,chromeStressNativePrints=0;
static unsigned chromeStressNativeMoves=0,chromeStressPaintDepth=0,chromeStressMaxPaintDepth=0;
struct ChromeStressMessage { UINT message; WPARAM wp; LPARAM lp; };
static std::vector<ChromeStressMessage> chromeStressMessageTrace;

static LRESULT CALLBACK ChromeStressNativeProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
  if(chromeRecordInvalidations && chromeStressMessageTrace.size()<64) chromeStressMessageTrace.push_back({message,wp,lp});
  // The real launcher's WM_DESTROY owns process-wide resources. This disposable
  // class owns only its window; all exercised input and paint messages below
  // travel through the production procedure via Windows' HWND dispatch.
  if(message==WM_DESTROY || message==WM_NCDESTROY) return DefWindowProcW(window,message,wp,lp);
  if(message==WM_MOUSEMOVE) ++chromeStressNativeMoves;
  if(message==WM_PAINT || message==WM_PRINTCLIENT) {
    if(message==WM_PAINT) ++chromeStressNativePaints;else ++chromeStressNativePrints;
    ++chromeStressPaintDepth;chromeStressMaxPaintDepth=max(chromeStressMaxPaintDepth,chromeStressPaintDepth);
    const auto result=WndProc(window,message,wp,lp);
    --chromeStressPaintDepth;
    return result;
  }
  return WndProc(window,message,wp,lp);
}

static void ChromeStressClearIcons() {
  std::lock_guard<std::mutex> lock(g_iconCacheMutex);
  for(auto &[name,cache]:g_iconCache) for(const auto &[px,icon]:cache.byPx) if(icon) DestroyIcon(icon);
  g_iconCache.clear();DestroyAllRetiredIconsLocked();
}

static void TestPinnedNativeHoverStress(const fs::path &captures) {
  const auto previousMain=g_hGui;const auto previousData=g_sDataDir;const auto previousDpi=g_uiDpi;
  const auto previousFont=g_hFont;const auto previousLabelFont=g_hFontLabel;
  const auto root=fs::absolute(captures.empty() ? fs::temp_directory_path()/std::format(L"ctSpaces-native-hover-{}-{}",GetCurrentProcessId(),GetTickCount64()) : captures/L"native-hover-fixture");
  g_sDataDir=root;
  std::vector<std::wstring> names;
  for(unsigned i=0;i<100;++i) {
    names.push_back(std::format(L"Pinned client {:03} - a realistic longer name",i));
    fs::create_directories(root/L"Sites"/names.back());
  }
  HICON resource=LoadIconW(g_hInst,MAKEINTRESOURCEW(IDI_CTSPACES));
  ChromeCheck(resource!=nullptr,"Native pinned-hover fixture has an application resource icon");
  std::vector<HICON> icons{resource};
  ChromeCheck(SaveIconsToFile(root/L"Sites"/names[0]/L"client.ico",icons,false,0),"Native pinned-hover fixture writes its own valid client icon");
  // Exercise both a malformed icon and absent icons without touching user data.
  {std::ofstream broken(root/L"Sites"/names[1]/L"client.ico",std::ios::binary);broken << "invalid icon";}

  const wchar_t *className=L"ctSpacesHiddenNativeHoverStress";
  WNDCLASSW windowClass{};windowClass.hInstance=g_hInst;windowClass.lpfnWndProc=ChromeStressNativeProc;windowClass.lpszClassName=className;
  ChromeCheck(RegisterClassW(&windowClass)!=0,"Register a native hidden window class forwarding to the production launcher procedure");
  g_hGui=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,className,L"Hidden native pinned hover fixture",WS_POPUP|WS_CLIPCHILDREN,0,0,600,234,nullptr,nullptr,g_hInst,nullptr);
  ChromeCheck(g_hGui!=nullptr && !IsWindowVisible(g_hGui),"Create the native launcher fixture hidden and without activation");
  HWND child=CreateWindowExW(0,L"BUTTON",L"Notes",WS_CHILD|BS_OWNERDRAW,420,4,68,26,g_hGui,reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_NOTES)),g_hInst,nullptr);
  HWND label=CreateWindowExW(0,L"STATIC",L"CLIENT",WS_CHILD,14,85,200,14,g_hGui,nullptr,g_hInst,nullptr);
  ChromeCheck(child && label && !IsWindowVisible(child) && !IsWindowVisible(label),"Native production parent retains hidden child controls throughout hover stress");

  std::ofstream timing;
  if(!captures.empty()) timing.open(captures/L"native-pinned-hover-metrics.csv",std::ios::trunc);
  if(timing) timing << "theme,simulated_render_dpi,pins,frames,native_paints,native_prints,empty_invisible_native_regions,cold_print_ms,mean_ms,p95_ms,max_ms,gdi_delta,user_delta,handle_delta\n";
  uint64_t allFrames=0,allEmptyInvisibleRegions=0;double largestMs=0;
  std::vector<std::string> contractFailures;
  const auto center=[](const RECT &r){return POINT{(r.left+r.right)/2,(r.top+r.bottom)/2};};
  const auto cacheIsValid=[&]() {
    if(g_pinnedClients.empty()) return true;
    std::lock_guard<std::mutex> lock(g_iconCacheMutex);
    const auto found=g_iconCache.find(names[0]);
    if(found==g_iconCache.end()) return false;
    const auto size=found->second.byPx.find(ScaleByDpi(20,g_uiDpi));
    return size!=found->second.byPx.end() && size->second!=nullptr;
  };

  for(const wchar_t *themeName:{L"Dark - Gothic",L"Marine"}) for(const UINT dpi:{96u,144u,192u}) for(const size_t pinCount:{0u,1u,4u,5u,100u}) {
    const auto theme=std::find_if(g_aCustomThemes.begin(),g_aCustomThemes.end(),[&](const ThemeEntry &entry){return entry.name==themeName;});
    ChromeCheck(theme!=g_aCustomThemes.end(),"Native pinned-hover stress uses an actual application theme");
    g_themeColors=theme->colors;g_bThemeIsDark=theme->dark;UpdateThemeBrushes();
    g_pinnedClients.assign(names.begin(),names.begin()+pinCount);g_uiDpi=dpi;g_bUiEnabled=true;
    const auto scale=[&](int value){return ScaleByDpi(value,dpi);};
    SetWindowPos(g_hGui,nullptr,0,0,scale(600),scale(234),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    // Physical monitor changes are deliberately out of scope in a hidden test.
    // These are production-renderer DIP layouts at three explicitly simulated DPIs.
    g_rcSessionTabs={0,0,scale(600),scale(34)};g_rcPinnedArea=pinCount ? RECT{scale(14),scale(38),scale(586),scale(77)} : RECT{};
    g_rcUtilityBar={0,scale(194),scale(600),scale(234)};g_rcBrowserSelector={scale(14),scale(199),scale(168),scale(229)};
    BuildPinnedClientRects();g_iHotPinnedClient=-1;g_bHotPinnedOverflow=false;g_bHotBrowserSelector=false;
    HFONT fixtureFont=CreateFontW(-scale(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    ChromeCheck(fixtureFont!=nullptr,"Native pinned-hover stress uses a DPI-scaled normal application font");g_hFont=fixtureFont;g_hFontLabel=fixtureFont;
    ChromeStressClearIcons();
    ChromeDib accumulated(g_hGui);accumulated.Clear(0x1493e2);
    const auto coldStart=std::chrono::steady_clock::now();
    SendMessageW(g_hGui,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(accumulated.dc),PRF_CLIENT); // Cold-cache print.
    const double coldMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-coldStart).count();
    ChromeCheck(cacheIsValid(),"Native cold-cache rendering loads the fixture's real client icon when pins exist");
    const auto resting=accumulated.Snapshot();
    std::vector<POINT> targets;
    for(const RECT &item:g_pinnedClientRects) {targets.push_back(center(item));targets.push_back(center(item));}
    if(g_rcPinnedOverflow.right>g_rcPinnedOverflow.left) {targets.push_back(center(g_rcPinnedOverflow));targets.push_back(center(g_rcPinnedOverflow));}
    targets.push_back(center(g_rcBrowserSelector));targets.push_back(center(g_rcBrowserSelector));targets.push_back(POINT{scale(590),scale(110)});
    // Warm the exact measured path, including hot-state rendering, dirty clip
    // selection, and complete-reference DIB comparisons for every target type.
    // Windows/GDI+ first-use resources are measured separately from steady
    // growth; the measured block below still requires a zero resource delta.
    bool warmPixelsExact=true;
    const DWORD warmGdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),warmUserBefore=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    DWORD warmHandlesBefore=0;GetProcessHandleCount(GetCurrentProcess(),&warmHandlesBefore);
    chromeRecordInvalidations=true;
    for(unsigned warm=0;warm<3;++warm) {
      for(size_t at=0;at<=targets.size();++at) {
        const bool leaving=at==targets.size();const POINT point=leaving ? POINT{} : targets[at];
        ValidateRect(g_hGui,nullptr);chromeInvalidations.clear();SendMessageW(g_hGui,leaving ? WM_MOUSELEAVE : WM_MOUSEMOVE,0,leaving ? 0 : MAKELPARAM(point.x,point.y));
        // Mirror the measured probe's three concurrently live region handles
        // and both union passes. The first warmup omitted these independent
        // comparison/probe allocations, which the stage log showed adding a
        // GDI object during the expected-region union, before painting ran.
        HRGN dirty=CreateRectRgn(0,0,0,0),requested=CreateRectRgn(0,0,0,0),actual=CreateRectRgn(0,0,0,0);
        for(const auto &change:chromeInvalidations) if(change.window==g_hGui) {HRGN part=CreateRectRgnIndirect(&change.bounds);CombineRgn(requested,requested,part,RGN_OR);DeleteObject(part);}
        for(const auto &change:chromeInvalidations) if(change.window==g_hGui) {HRGN part=CreateRectRgnIndirect(&change.bounds);CombineRgn(dirty,dirty,part,RGN_OR);DeleteObject(part);}
        GetUpdateRgn(g_hGui,actual,FALSE);DeleteObject(actual);DeleteObject(requested);
        SendMessageW(g_hGui,WM_PAINT,0,0);
        if(GetUpdateRect(g_hGui,nullptr,FALSE)) warmPixelsExact=false;
        const int saved=SaveDC(accumulated.dc);SelectClipRgn(accumulated.dc,dirty);
        SendMessageW(g_hGui,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(accumulated.dc),PRF_CLIENT);RestoreDC(accumulated.dc,saved);DeleteObject(dirty);
        ChromeDib complete(g_hGui);SendMessageW(g_hGui,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(complete.dc),PRF_CLIENT);
        if(accumulated.Snapshot()!=complete.Snapshot()) warmPixelsExact=false;
      }
    }
    chromeRecordInvalidations=false;GdiFlush();
    ValidateRect(g_hGui,nullptr);
    const DWORD gdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),userBefore=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    DWORD handlesBefore=0;GetProcessHandleCount(GetCurrentProcess(),&handlesBefore);
    std::cout << "NATIVE HOVER EXACT-PATH WARMUP: theme=" << (g_bThemeIsDark ? "dark" : "light") << "; dpi=" << dpi << "; pins=" << pinCount << "; cycles=3; before_GDI/USER/handles=" << warmGdiBefore << '/' << warmUserBefore << '/' << warmHandlesBefore << "; after_GDI/USER/handles=" << gdiBefore << '/' << userBefore << '/' << handlesBefore << "; pixels_exact=" << warmPixelsExact << std::endl;
    std::cout << "NATIVE HOVER RESOURCE BASELINE: theme=" << (g_bThemeIsDark ? "dark" : "light") << "; dpi=" << dpi << "; pins=" << pinCount << "; GDI/USER/handles=" << gdiBefore << '/' << userBefore << '/' << handlesBefore << std::endl;
    const unsigned paintBefore=chromeStressNativePaints,printBefore=chromeStressNativePrints,moveBefore=chromeStressNativeMoves;
    bool invalidationsBounded=true,pixelsExact=warmPixelsExact,regionsExact=true,paintSettles=true,restExact=accumulated.Snapshot()==resting,printedFailure=false,printedPixelFailure=false;
    uint64_t emptyInvisibleRegions=0;
    std::vector<double> samples;
    chromeRecordInvalidations=true;chromeRecordRedraws=true;
    for(unsigned cycle=0;cycle<20;++cycle) {
      for(size_t at=0;at<=targets.size();++at) {
        const bool traceResources=g_bThemeIsDark && dpi==96 && pinCount<=1 && cycle==0;
        const auto resourceStage=[&](const char *stage) {
          if(traceResources) {GdiFlush();DWORD handles=0;GetProcessHandleCount(GetCurrentProcess(),&handles);
            std::cout << "NATIVE HOVER RESOURCE STAGE: pins=" << pinCount << "; at=" << at << "; stage=" << stage << "; GDI/USER/handles=" << GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS) << '/' << GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS) << '/' << handles << std::endl;}
        };
        resourceStage("frame start");
        const int oldPin=g_iHotPinnedClient;const bool oldOverflow=g_bHotPinnedOverflow,oldBrowser=g_bHotBrowserSelector;
        std::vector<RECT> expected;
        const bool leaving=at==targets.size();const POINT point=leaving ? POINT{} : targets[at];
        int newPin=-1;if(!leaving) for(size_t i=0;i<g_pinnedClientRects.size();++i) if(PtInRect(&g_pinnedClientRects[i],point)) newPin=static_cast<int>(i);
        const bool newOverflow=!leaving && PtInRect(&g_rcPinnedOverflow,point),newBrowser=!leaving && PtInRect(&g_rcBrowserSelector,point);
        if(oldPin!=newPin) for(const int index:{oldPin,newPin}) if(index>=0 && static_cast<size_t>(index)<g_pinnedClientRects.size()) expected.push_back(g_pinnedClientRects[index]);
        if(oldOverflow!=newOverflow) expected.push_back(g_rcPinnedOverflow);
        if(oldBrowser!=newBrowser) {
          // The production GDI+ border has a one-physical-pixel antialias
          // footprint outside the logical selector rectangle at every DPI.
          RECT borderFootprint=g_rcBrowserSelector;InflateRect(&borderFootprint,1,1);expected.push_back(borderFootprint);
        }
        ValidateRect(g_hGui,nullptr);chromeInvalidations.clear();chromeRedrawRequests.clear();chromeStressMessageTrace.clear();
        const auto start=std::chrono::steady_clock::now();
        SendMessageW(g_hGui,leaving ? WM_MOUSELEAVE : WM_MOUSEMOVE,0,leaving ? 0 : MAKELPARAM(point.x,point.y));
        resourceStage("after native hover");
        bool frameBounded=chromeInvalidations.size()==expected.size();
        for(const auto &change:chromeInvalidations) {
          bool matches=false;for(const RECT &item:expected) if(EqualRect(&item,&change.bounds)) matches=true;
          if(change.window!=g_hGui || change.erase || !matches) frameBounded=false;
        }
        if(!frameBounded) {
          invalidationsBounded=false;
          if(!printedFailure) {
            const auto rectText=[](const RECT &r){return std::format("[{},{},{},{}]",r.left,r.top,r.right,r.bottom);};
            std::cout << "NATIVE HOVER INVALIDATION DIAGNOSTIC: theme=" << (g_bThemeIsDark ? "dark" : "light") << "; dpi=" << dpi << "; pins=" << pinCount << "; cycle=" << cycle << "; at=" << at << "; leaving=" << leaving << "; xy=" << point.x << ',' << point.y << "; before_pin/overflow/browser=" << oldPin << '/' << oldOverflow << '/' << oldBrowser << "; wanted=" << newPin << '/' << newOverflow << '/' << newBrowser << "; after=" << g_iHotPinnedClient << '/' << g_bHotPinnedOverflow << '/' << g_bHotBrowserSelector << "; expected_count=" << expected.size() << "; actual_count=" << chromeInvalidations.size() << "; pinned=" << rectText(g_rcPinnedArea) << "; overflow=" << rectText(g_rcPinnedOverflow) << "; browser=" << rectText(g_rcBrowserSelector) << std::endl;
            for(const RECT &item:expected) std::cout << "  expected " << rectText(item) << std::endl;
            for(const auto &change:chromeInvalidations) std::cout << "  actual window=" << reinterpret_cast<uintptr_t>(change.window) << "; parent=" << reinterpret_cast<uintptr_t>(g_hGui) << "; erase=" << change.erase << "; bounds=" << rectText(change.bounds) << std::endl;
            for(const auto &request:chromeRedrawRequests) std::cout << "  redraw window=" << reinterpret_cast<uintptr_t>(request.window) << "; flags=" << request.flags << std::endl;
            for(const auto &entry:chromeStressMessageTrace) std::cout << "  native message=" << entry.message << "; wp=" << entry.wp << "; lp=" << entry.lp << std::endl;
            printedFailure=true;
          }
        }
        HRGN dirty=CreateRectRgn(0,0,0,0),requested=CreateRectRgn(0,0,0,0),actual=CreateRectRgn(0,0,0,0);
        resourceStage("after three region allocations");
        if(traceResources) std::cout << "  native region handle types dirty/requested/actual=" << GetObjectType(dirty) << '/' << GetObjectType(requested) << '/' << GetObjectType(actual) << "; expected OBJ_REGION=" << OBJ_REGION << std::endl;
        for(const RECT &item:expected) {HRGN part=CreateRectRgnIndirect(&item);CombineRgn(requested,requested,part,RGN_OR);DeleteObject(part);}
        resourceStage("after expected region union");
        // Feed the renderer the rectangles actually requested by production,
        // independently checked above against the expected state transition.
        for(const auto &change:chromeInvalidations) if(change.window==g_hGui) {
          HRGN part=CreateRectRgnIndirect(&change.bounds);CombineRgn(dirty,dirty,part,RGN_OR);DeleteObject(part);
        }
        resourceStage("after actual region union");
        GetUpdateRgn(g_hGui,actual,FALSE);
        resourceStage("after hidden GetUpdateRgn");
        if(!EqualRgn(requested,actual)) {
          RECT expectedBox{},actualBox{};const int expectedKind=GetRgnBox(requested,&expectedBox),actualKind=GetRgnBox(actual,&actualBox);
          ChromeRequireOwnedWindow(g_hGui);
          // USER may discard invalidation regions on an invisible HWND. This
          // is an observed fixture limitation, not a retained-region pass.
          // Any retained native region must still equal the request exactly.
          const bool discardedInvisible=actualKind==NULLREGION && !IsWindowVisible(g_hGui);
          if(discardedInvisible) ++emptyInvisibleRegions;
          else regionsExact=false;
          if(!discardedInvisible && !printedFailure) {
            const auto rectText=[](const RECT &r){return std::format("[{},{},{},{}]",r.left,r.top,r.right,r.bottom);};
            std::cout << "NATIVE HOVER UPDATE REGION DIAGNOSTIC: theme=" << (g_bThemeIsDark ? "dark" : "light") << "; dpi=" << dpi << "; pins=" << pinCount << "; cycle=" << cycle << "; at=" << at << "; leaving=" << leaving << "; xy=" << point.x << ',' << point.y << "; before_pin/overflow/browser=" << oldPin << '/' << oldOverflow << '/' << oldBrowser << "; wanted=" << newPin << '/' << newOverflow << '/' << newBrowser << "; after=" << g_iHotPinnedClient << '/' << g_bHotPinnedOverflow << '/' << g_bHotBrowserSelector << "; region_kind_expected/actual=" << expectedKind << '/' << actualKind << "; region_box_expected/actual=" << rectText(expectedBox) << '/' << rectText(actualBox) << "; visible=" << IsWindowVisible(g_hGui) << std::endl;
            for(const RECT &item:expected) std::cout << "  expected " << rectText(item) << std::endl;
            for(const auto &change:chromeInvalidations) std::cout << "  actual window=" << reinterpret_cast<uintptr_t>(change.window) << "; parent=" << reinterpret_cast<uintptr_t>(g_hGui) << "; erase=" << change.erase << "; bounds=" << rectText(change.bounds) << std::endl;
            for(const auto &request:chromeRedrawRequests) std::cout << "  redraw window=" << reinterpret_cast<uintptr_t>(request.window) << "; flags=" << request.flags << std::endl;
            for(const auto &entry:chromeStressMessageTrace) std::cout << "  native message=" << entry.message << "; wp=" << entry.wp << "; lp=" << entry.lp << std::endl;
            printedFailure=true;
          }
        }
        DeleteObject(actual);DeleteObject(requested);
        resourceStage("after deleting actual and requested regions");
        // A hidden WM_PAINT has no desktop surface. Its real Begin/EndPaint path
        // must consume the update region without starting another invalidation.
        const auto invalidationCount=chromeInvalidations.size();
        SendMessageW(g_hGui,WM_PAINT,0,0);
        resourceStage("after native WM_PAINT");
        if(GetUpdateRect(g_hGui,nullptr,FALSE) || chromeInvalidations.size()!=invalidationCount) paintSettles=false;
        const int saved=SaveDC(accumulated.dc);SelectClipRgn(accumulated.dc,dirty);resourceStage("after selecting dirty clip");
        SendMessageW(g_hGui,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(accumulated.dc),PRF_CLIENT);resourceStage("after native partial print");
        RestoreDC(accumulated.dc,saved);DeleteObject(dirty);resourceStage("after restoring DC and deleting dirty region");
        const auto end=std::chrono::steady_clock::now();samples.push_back(std::chrono::duration<double,std::milli>(end-start).count());
        // Check every state in the first and last cycles against a complete
        // production paint, including dividers shared by adjacent pin items.
        if(cycle==0 || cycle==19) {
          ChromeDib full(g_hGui);resourceStage("after fresh reference DIB allocation");SendMessageW(g_hGui,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(full.dc),PRF_CLIENT);resourceStage("after full reference print");
          const auto actualPixels=accumulated.Snapshot(),fullPixels=full.Snapshot();
          if(actualPixels!=fullPixels) {
            pixelsExact=false;
            if(!printedPixelFailure) {
              size_t first=0,count=0;RECT changed{accumulated.width,accumulated.height,0,0};
              for(size_t index=0;index<actualPixels.size();++index) if(actualPixels[index]!=fullPixels[index]) {
                if(!count) first=index;++count;const int x=static_cast<int>(index%accumulated.width),y=static_cast<int>(index/accumulated.width);
                changed.left=min(changed.left,x);changed.top=min(changed.top,y);changed.right=max(changed.right,x+1);changed.bottom=max(changed.bottom,y+1);
              }
              const auto rectText=[](const RECT &r){return std::format("[{},{},{},{}]",r.left,r.top,r.right,r.bottom);};
              std::cout << "NATIVE HOVER PIXEL DIAGNOSTIC: theme=" << (g_bThemeIsDark ? "dark" : "light") << "; dpi=" << dpi << "; pins=" << pinCount << "; cycle=" << cycle << "; at=" << at << "; leaving=" << leaving << "; xy=" << point.x << ',' << point.y << "; before_pin/overflow/browser=" << oldPin << '/' << oldOverflow << '/' << oldBrowser << "; wanted=" << newPin << '/' << newOverflow << '/' << newBrowser << "; after=" << g_iHotPinnedClient << '/' << g_bHotPinnedOverflow << '/' << g_bHotBrowserSelector << "; firstdiff_xy=" << first%accumulated.width << ',' << first/accumulated.width << "; actual/complete/resting_rgb=" << actualPixels[first] << '/' << fullPixels[first] << '/' << resting[first] << "; changed_count=" << count << "; changed_box=" << rectText(changed) << "; client=" << accumulated.width << 'x' << accumulated.height << "; tabs=" << rectText(g_rcSessionTabs) << "; pins=" << rectText(g_rcPinnedArea) << "; utility=" << rectText(g_rcUtilityBar) << "; selector=" << rectText(g_rcBrowserSelector) << std::endl;
              for(const auto &change:chromeInvalidations) std::cout << "  pixel actual invalidation window=" << reinterpret_cast<uintptr_t>(change.window) << "; erase=" << change.erase << "; bounds=" << rectText(change.bounds) << std::endl;
              for(const auto &entry:chromeStressMessageTrace) std::cout << "  pixel native message=" << entry.message << "; wp=" << entry.wp << "; lp=" << entry.lp << std::endl;
              if(!captures.empty()) {
                const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
                const auto prefix=std::format(L"native-hover-first-pixel-diff-{}-{}-{}",g_bThemeIsDark ? L"dark" : L"light",dpi,pinCount);
                Gdiplus::Bitmap accumulatedBitmap(accumulated.bitmap,nullptr),fullBitmap(full.bitmap,nullptr);
                ChromeCheck(accumulatedBitmap.Save((captures/(prefix+L"-actual.png")).c_str(),&png,nullptr)==Gdiplus::Ok,"Save accumulated native dirty-region pixel diagnostic");
                ChromeCheck(fullBitmap.Save((captures/(prefix+L"-complete.png")).c_str(),&png,nullptr)==Gdiplus::Ok,"Save complete native reference pixel diagnostic");
                ChromeDib difference(g_hGui);GdiFlush();
                for(size_t index=0;index<actualPixels.size();++index) difference.pixels[index]=actualPixels[index]==fullPixels[index] ? 0xff000000 : 0xffff00ff;
                Gdiplus::Bitmap differenceBitmap(difference.bitmap,nullptr);
                ChromeCheck(differenceBitmap.Save((captures/(prefix+L"-difference.png")).c_str(),&png,nullptr)==Gdiplus::Ok,"Save first native dirty-region mismatch mask");
              }
              printedPixelFailure=true;
            }
          }
          if(leaving && actualPixels!=resting) {
            restExact=false;
            if(!printedPixelFailure) {
              size_t first=0;while(first<actualPixels.size() && actualPixels[first]==resting[first]) ++first;
              std::cout << "NATIVE HOVER RESTING DIAGNOSTIC: theme=" << (g_bThemeIsDark ? "dark" : "light") << "; dpi=" << dpi << "; pins=" << pinCount << "; cycle=" << cycle << "; firstdiff_xy=" << first%accumulated.width << ',' << first/accumulated.width << "; actual/resting_rgb=" << actualPixels[first] << '/' << resting[first] << std::endl;
              printedPixelFailure=true;
            }
          }
        }
        resourceStage("after full reference DIB destruction");
      }
      if(cycle==0 || cycle==9 || cycle==19) {
        GdiFlush();DWORD handles=0;GetProcessHandleCount(GetCurrentProcess(),&handles);
        std::cout << "NATIVE HOVER RESOURCE CHECKPOINT: cycle=" << cycle << "; GDI/USER/handles=" << GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS) << '/' << GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS) << '/' << handles << std::endl;
      }
    }
    chromeRecordInvalidations=false;chromeRecordRedraws=false;GdiFlush();
    const DWORD gdiAfter=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),userAfter=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    DWORD handlesAfter=0;GetProcessHandleCount(GetCurrentProcess(),&handlesAfter);
    std::cout << "NATIVE HOVER RESOURCE DIAGNOSTIC: theme=" << (g_bThemeIsDark ? "dark" : "light") << "; dpi=" << dpi << "; pins=" << pinCount << "; before_GDI/USER/handles=" << gdiBefore << '/' << userBefore << '/' << handlesBefore << "; after_GDI/USER/handles=" << gdiAfter << '/' << userAfter << '/' << handlesAfter << std::endl;
    std::sort(samples.begin(),samples.end());double total=0;for(const double value:samples) total+=value;
    const double p95=samples[(samples.size()-1)*95/100],peak=samples.back();largestMs=max(largestMs,peak);allFrames+=samples.size();allEmptyInvisibleRegions+=emptyInvisibleRegions;
    // Record latency rather than treating an environment-dependent deadline as
    // evidence that the compositor is visually smooth.
    const bool nativeCounts=chromeStressNativePaints-paintBefore==samples.size() && chromeStressNativeMoves-moveBefore==targets.size()*20 && chromeStressNativePrints>printBefore;
    const bool resourcesStable=gdiAfter<=gdiBefore && userAfter<=userBefore && handlesAfter<=handlesBefore;
    std::cout << "Native pinned scenario: theme=" << (g_bThemeIsDark ? "dark" : "light") << "; simulated_render_dpi=" << dpi << "; pins=" << pinCount << "; frames=" << samples.size() << "; empty_invisible_native_regions=" << emptyInvisibleRegions << "; cold_print_ms=" << coldMs << "; mean/p95/max_ms=" << total/samples.size() << "/" << p95 << "/" << peak << "; GDI/USER/handle_delta=" << static_cast<long long>(gdiAfter)-gdiBefore << "/" << static_cast<long long>(userAfter)-userBefore << "/" << static_cast<long long>(handlesAfter)-handlesBefore << "; invalidations/regions/pixels/rest/settled/nativeCounts/resources=" << invalidationsBounded << '/' << regionsExact << '/' << pixelsExact << '/' << restExact << '/' << paintSettles << '/' << nativeCounts << '/' << resourcesStable << "; max_paint_depth=" << chromeStressMaxPaintDepth << std::endl;
    if(timing) timing << (g_bThemeIsDark ? "dark" : "light") << ',' << dpi << ',' << pinCount << ',' << samples.size() << ',' << chromeStressNativePaints-paintBefore << ',' << chromeStressNativePrints-printBefore << ',' << emptyInvisibleRegions << ',' << coldMs << ',' << total/samples.size() << ',' << p95 << ',' << peak << ',' << static_cast<long long>(gdiAfter)-gdiBefore << ',' << static_cast<long long>(userAfter)-userBefore << ',' << static_cast<long long>(handlesAfter)-handlesBefore << '\n';
    const auto expect=[&](bool condition,const char *message) {
      if(condition) ChromeCheck(true,message);
      else {
        contractFailures.push_back(std::format("theme={}, dpi={}, pins={}: {}",g_bThemeIsDark ? "dark" : "light",dpi,pinCount,message));
        std::cout << "FAIL CONTRACT (collecting remaining hidden scenarios): " << contractFailures.back() << std::endl;
      }
    };
    expect(invalidationsBounded,"Native pinned hover and leave request exactly the changing items without background erase");
    expect(regionsExact,"Any retained hidden native update region equals the request; discarded invisible regions are counted separately");
    expect(pixelsExact && restExact,"Native dirty-region updates equal complete themed pin images and restore the exact resting image");
    expect(paintSettles && chromeStressMaxPaintDepth==1,"Native WM_PAINT consumes every hover update without reentrant painting or a repaint feedback loop");
    expect(nativeCounts,"All stressed hover and paint requests travel through the registered native window procedure");
    expect(resourcesStable,"Repeated native hover and leave do not grow steady GDI, USER, or process handle resources");
    g_hFont=previousFont;g_hFontLabel=previousLabelFont;DeleteObject(fixtureFont);
  }
  ChromeStressClearIcons();chromeRecordInvalidations=false;
  DestroyWindow(g_hGui);g_hGui=previousMain;UnregisterClassW(className,g_hInst);
  g_sDataDir=previousData;g_uiDpi=previousDpi;g_pinnedClients.clear();g_pinnedClientRects.clear();
  g_rcPinnedOverflow={};g_iHotPinnedClient=-1;g_bHotPinnedOverflow=false;g_bHotBrowserSelector=false;
  std::cout << "Native pinned-hover stress: frames=" << allFrames << "; empty_invisible_native_regions=" << allEmptyInvisibleRegions << "; peak observed local dispatch/paint ms=" << largestMs << "; visible USER update-region and compositor coverage excluded." << std::endl;
  if(!contractFailures.empty()) {
    // Do not let a contract failure bypass the process-wide native event audit.
    // Its counters were installed by wmain before any fixtures were created.
    ChromeNativeObserver audit; audit.Audit();
    std::string failure="Native pinned-hover stress contract failures: "+std::to_string(contractFailures.size());
    for(const auto &item:contractFailures) failure+='\n'+item;
    throw std::runtime_error(failure);
  }
}
