// These fixtures never show a window or synthesize user input. They route the
// launcher's native WM_COMMAND into the production command handler, and ask
// real native child classes to print into an in-memory DIB. The parent's real
// WM_PAINT path uses the harness-only BeginPaint target above. USER discards
// hidden-window update regions, so the parent test supplies its DIB clip
// explicitly while retaining the native BeginPaint/EndPaint validation pair.
// This exercises initialization, native owner-draw dispatch and instrumented
// paint output/scheduling, not visible-window update coverage or DWM timing.
struct NotesNormalMessageCounts {
  unsigned paint=0,print=0,erase=0,draw=0,streamIn=0,streamOut=0;
  unsigned geometry=0,theme=0,font=0,documentFormat=0,init=0;
  std::map<UINT,unsigned> drawIds;
  DRAWITEMSTRUCT lastDraw{};
};
struct NotesNormalTrace {
  HWND owner=nullptr;
  HHOOK hook=nullptr;
  NotesNormalMessageCounts counts;
  static inline NotesNormalTrace *current=nullptr;
  static LRESULT CALLBACK Observe(int code,WPARAM w,LPARAM l) {
    if(code>=0 && current) {
      const auto &event=*reinterpret_cast<CWPSTRUCT *>(l);
      if(!current->owner && event.message==WM_INITDIALOG) current->owner=event.hwnd;
      if(current->owner && (event.hwnd==current->owner || IsChild(current->owner,event.hwnd))) {
        auto &c=current->counts;
        switch(event.message) {
        case WM_INITDIALOG:++c.init;break;
        case WM_PAINT:++c.paint;break;
        case WM_PRINT:case WM_PRINTCLIENT:++c.print;break;
        case WM_ERASEBKGND:++c.erase;break;
        case WM_DRAWITEM:
          if(event.lParam) {++c.draw;c.lastDraw=*reinterpret_cast<const DRAWITEMSTRUCT *>(event.lParam);++c.drawIds[c.lastDraw.CtlID];}break;
        case EM_STREAMIN:++c.streamIn;break;
        case EM_STREAMOUT:++c.streamOut;break;
        case WM_SIZE:case WM_WINDOWPOSCHANGED:++c.geometry;break;
        case WM_THEMECHANGED:case WM_SYSCOLORCHANGE:++c.theme;break;
        case WM_SETFONT:++c.font;break;
        case EM_SETCHARFORMAT:case EM_SETPARAFORMAT:case EM_SETBKGNDCOLOR:++c.documentFormat;break;
        }
      }
    }
    return CallNextHookEx(current ? current->hook : nullptr,code,w,l);
  }
  NotesNormalTrace() {
    Check(!current,"Only one hidden native opening trace owns the current test thread");current=this;
    hook=SetWindowsHookExW(WH_CALLWNDPROC,Observe,nullptr,GetCurrentThreadId());
    Check(hook!=nullptr,"Native in-process Notes message tracing is installed without an input hook");
  }
  ~NotesNormalTrace(){if(hook) UnhookWindowsHookEx(hook);current=nullptr;}
  void Reset(){counts={};}
};
struct NotesNormalDib {
  int width=0,height=0;
  HWND owner=nullptr;HDC reference=nullptr,dc=nullptr;
  HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;DWORD *pixels=nullptr;
  explicit NotesNormalDib(HWND window) : owner(window) {
    RECT bounds{};GetClientRect(window,&bounds);width=(std::max)(1L,bounds.right);height=(std::max)(1L,bounds.bottom);
    reference=GetDC(window);dc=CreateCompatibleDC(reference);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    bitmap=CreateDIBSection(reference,&info,DIB_RGB_COLORS,reinterpret_cast<void **>(&pixels),nullptr,0);
    if(!dc || !bitmap || !pixels) throw std::runtime_error("Normal Notes native-paint DIB allocation failed");
    previous=SelectObject(dc,bitmap);
  }
  ~NotesNormalDib(){if(previous) SelectObject(dc,previous);if(bitmap) DeleteObject(bitmap);if(dc) DeleteDC(dc);if(reference) ReleaseDC(owner,reference);}
  void Clear(DWORD marker=0x005A27C3){GdiFlush();std::fill(pixels,pixels+width*height,marker);}
  std::vector<DWORD> Read(){GdiFlush();std::vector<DWORD> copy(pixels,pixels+width*height);for(auto &pixel:copy) pixel&=0xffffff;return copy;}
};
static LRESULT CALLBACK NotesNormalMainDispatch(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR) {
  if(message==WM_COMMAND && LOWORD(w)==IDC_BTN_NOTES) return WndProc(window,message,w,l);
  if(message==WM_NCDESTROY) RemoveWindowSubclass(window,NotesNormalMainDispatch,id);
  return DefSubclassProc(window,message,w,l);
}
static void NotesNormalValidateTree(HWND owner) {
  ValidateRect(owner,nullptr);
  EnumChildWindows(owner,[](HWND child,LPARAM)->BOOL {ValidateRect(child,nullptr);return TRUE;},0);
}
static unsigned NotesNormalDirtyTree(HWND owner) {
  unsigned dirty=GetUpdateRect(owner,nullptr,FALSE)!=FALSE;
  EnumChildWindows(owner,[](HWND child,LPARAM count)->BOOL {if(GetUpdateRect(child,nullptr,FALSE)) ++*reinterpret_cast<unsigned *>(count);return TRUE;},reinterpret_cast<LPARAM>(&dirty));
  return dirty;
}
static bool NotesNormalQuiet(const NotesNormalMessageCounts &c) {
  return !c.streamIn && !c.streamOut && !c.geometry && !c.theme && !c.font && !c.documentFormat && !c.init;
}
static void NotesNormalObserveRepaints() {
  notesObservedInvalidates=notesObservedRedraws=0;notesObserveRepaintRequests=true;
}
static void NotesNormalCheckNoRepaints(const char *description) {
  notesObserveRepaintRequests=false;
  Check(!notesObservedInvalidates && !notesObservedRedraws,description);
}
static void NotesNormalNativeChildren(HWND owner,NotesNormalTrace &trace,unsigned &nativePrints,unsigned &ownerDispatches) {
  const auto &state=State(owner);
  for(const int id:{IDCANCEL,IDC_NOTES_SWITCH_CLIENT,IDC_NOTES_LIBRARY,IDC_NOTES_TRASH,IDC_NOTES_MORE,IDC_NOTES_HELP,
      IDC_NOTES_FIND_TOGGLE,IDC_NOTES_SNIPPETS,IDC_NOTES_TEMPLATES,IDC_NOTES_DETAILS,IDC_NOTES_OPEN_TICKET,
      IDC_NOTES_WORKLOG,IDC_NOTES_FORMAT,IDC_NOTES_ADD_TAB,IDC_NOTES_BOLD,IDC_NOTES_HIGHLIGHT_COLOR,
      IDC_NOTES_TICKET_REFERENCE,IDC_NOTES_WORK_STATUS,IDC_NOTES_STYLE,IDC_NOTES_SIZE,IDC_NOTES_TABS}) {
    HWND child=GetDlgItem(owner,id);Check(child && IsWindow(child),"Normal Notes keeps each native painted child available");
    NotesNormalDib target(child);target.Clear();trace.Reset();observedPaintTarget=target.dc;observedPaintPublishes=0;NotesNormalObserveRepaints();
    SendMessageW(child,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(target.dc),PRF_CLIENT);
    observedPaintTarget=nullptr;++nativePrints;
    NotesNormalCheckNoRepaints("Native Notes child printing requests no production invalidation or redraw feedback");
    const auto actual=target.Read();const auto native=trace.counts;
    const bool combo=id==IDC_NOTES_STYLE || id==IDC_NOTES_SIZE;
    const bool tabs=id==IDC_NOTES_TABS;
    Check(observedPaintPublishes==1,"A real native Notes child print publishes exactly one finished surface");
    Check(native.print>=1 && NotesNormalQuiet(native),"Native child printing never reloads text, reapplies themes/fonts or changes window geometry");
    if(!combo && !tabs) {
      Check(native.draw==1 && native.drawIds.contains(id) && native.drawIds.at(id)==1,"The actual native Button/Static class dispatches one owner-draw request to Notes");
      ++ownerDispatches;
      auto draw=native.lastDraw;draw.hDC=target.dc;target.Clear();
      Check(NotesRedrawKnownItem(draw,state,true) && target.Read()==actual,"Native owner-draw output matches the established unbuffered appearance");
    }
    target.Clear(0x002C8B51);trace.Reset();observedPaintTarget=target.dc;observedPaintPublishes=0;
    SendMessageW(child,WM_ERASEBKGND,reinterpret_cast<WPARAM>(target.dc),0);
    const auto erased=target.Read();
    Check(std::all_of(erased.begin(),erased.end(),[](DWORD pixel){return pixel==0x002C8B51;}),"Native erase leaves opaque Notes child pixels untouched");
    SendMessageW(child,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(target.dc),PRF_CLIENT);
    observedPaintTarget=nullptr;++nativePrints;
    Check(observedPaintPublishes==1 && target.Read()==actual,"Native Notes child paint covers the entire client independently of its previous contents");
  }
}
static void NotesNormalParentPaint(HWND owner,NotesNormalTrace &trace,unsigned &parentPaints) {
  NotesNormalDib target(owner);
  unsigned emptyNativeRegions=0;
  auto paint=[&](const RECT *dirty) {
    InvalidateRect(owner,dirty,FALSE);
    notesVirtualPaintWindow=owner;notesVirtualPaintDc=target.dc;
    notesVirtualClipRequested=true;
    notesVirtualRequestedDirty=dirty ? *dirty : RECT{0,0,target.width,target.height};
    notesVirtualPaintBegins=notesVirtualPaintEnds=notesVirtualPaintFrames=0;observedPaintTarget=target.dc;observedPaintPublishes=0;trace.Reset();
    notesVirtualPaintFrameRects.clear();
    NotesNormalObserveRepaints();
    SendMessageW(owner,WM_PAINT,0,0);
    notesVirtualPaintWindow=nullptr;notesVirtualPaintDc=nullptr;notesVirtualClipRequested=false;observedPaintTarget=nullptr;++parentPaints;
    emptyNativeRegions+=notesVirtualUpdateKind==NULLREGION;
    NotesNormalCheckNoRepaints("The actual Notes parent painter requests no invalidation or redraw feedback");
    Check(notesVirtualPaintBegins==1 && notesVirtualPaintEnds==1 && observedPaintPublishes==1,"The normal Notes WM_PAINT path validates once and publishes one finished surface into its explicit DIB clip");
    const auto &state=State(owner);
    std::vector<std::pair<int,RECT>> expectedFrames;
    for(const int id:{IDC_CLIENT_NOTES_TEXT,IDC_NOTES_FIND,IDC_NOTES_REPLACE_TEXT}) {
      if((id==IDC_NOTES_FIND && !state.findVisible) ||
          (id==IDC_NOTES_REPLACE_TEXT && (!state.findVisible || !state.replaceVisible))) continue;
      const auto child=GetDlgItem(owner,id);if(!child) continue;
      RECT bounds{};Check(GetWindowRect(child,&bounds)!=FALSE,"An expected Notes frame has a real native child rectangle");
      MapWindowPoints(nullptr,owner,reinterpret_cast<POINT *>(&bounds),2);InflateRect(&bounds,1,1);
      expectedFrames.emplace_back(id,bounds);
    }
    if(!state.findVisible)
      Check(notesVirtualPaintFrames==(GetDlgItem(owner,IDC_CLIENT_NOTES_TEXT) ? 1u : 0u),"Parent paint outlines an existing editor exactly once and never paints a phantom outline in an empty notebook");
    Check(notesVirtualPaintFrames==expectedFrames.size() && notesVirtualPaintFrameRects.size()==expectedFrames.size(),"Parent paint outlines exactly the editor and logically open Find or Replace fields, without hidden-search or phantom frames");
    for(size_t frame=0;frame<expectedFrames.size() && frame<notesVirtualPaintFrameRects.size();++frame) {
      const auto &[id,bounds]=expectedFrames[frame];const auto &[actual,brush]=notesVirtualPaintFrameRects[frame];
      Check(EqualRect(&bounds,&actual) && brush==g_hbrThemeBorder,"Each parent frame has its intended control's exact native rectangle and selected-theme border brush");
      std::cout<<"Normal Notes frame control="<<id<<" bounds="<<actual.left<<','<<actual.top<<','<<actual.right<<','<<actual.bottom<<"; find="<<state.findVisible<<"; replace="<<state.replaceVisible<<'\n';
    }
    Check(trace.counts.paint==1 && NotesNormalQuiet(trace.counts),"Parent WM_PAINT never streams notes, repeats layout or reapplies a theme/font");
    Check(!GetUpdateRect(owner,nullptr,FALSE),"The normal Notes paint consumes its update region without scheduling itself again");
  };
  target.Clear();paint(nullptr);const auto first=target.Read();
  target.Clear(0x002C8B51);paint(nullptr);
  const auto second=target.Read();
  if(second!=first) {
    const auto mismatch=std::mismatch(first.begin(),first.end(),second.begin());
    const size_t at=static_cast<size_t>(mismatch.first-first.begin());
    unsigned changed=0,firstMarker=0,secondMarker=0;
    for(size_t i=0;i<first.size();++i) {changed+=first[i]!=second[i];firstMarker+=first[i]==0x005A27C3;secondMarker+=second[i]==0x002C8B51;}
    const auto rect=[](const RECT &area){return std::to_string(area.left)+","+std::to_string(area.top)+","+std::to_string(area.right)+","+std::to_string(area.bottom);};
    std::cout<<"Normal parent paint mismatch; size="<<target.width<<"x"<<target.height<<"; first="<<(at%target.width)<<","<<(at/target.width)
        <<"; pixels="<<std::hex<<first[at]<<"/"<<second[at]<<std::dec<<"; changed="<<changed<<"; sentinel="<<firstMarker<<"/"<<secondMarker
        <<"; native update kind="<<notesVirtualUpdateKind<<"; native update bounds="<<rect(notesVirtualUpdateBounds)
        <<"; native clip="<<rect(notesVirtualNativeClip)<<"; native paint rect="<<rect(notesVirtualPaintRect)<<std::endl;
    const fs::path captures=fs::current_path()/L"build/notes-live-review-2026-10-19/normal-parent-diagnostic"/
        (std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));fs::create_directories(captures);
    const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
    GdiFlush();std::copy(first.begin(),first.end(),target.pixels);Gdiplus::Bitmap a(target.bitmap,nullptr);a.Save((captures/L"parent-first.png").c_str(),&png,nullptr);
    GdiFlush();std::copy(second.begin(),second.end(),target.pixels);Gdiplus::Bitmap b(target.bitmap,nullptr);b.Save((captures/L"parent-second.png").c_str(),&png,nullptr);
  }
  Check(second==first,"Instrumented full-clip normal Notes chrome has no uninitialized or previous-frame pixels");
  RECT patch{19,23,(std::min)(target.width,119),(std::min)(target.height,83)};
  target.Clear();paint(&patch);const auto partial=target.Read();bool exact=true;
  for(int y=0;y<target.height && exact;++y) for(int x=0;x<target.width;++x) {
    const size_t at=static_cast<size_t>(y)*target.width+x;
    const DWORD expected=x>=patch.left && x<patch.right && y>=patch.top && y<patch.bottom ? first[at] : 0x005A27C3;
    if(partial[at]!=expected){exact=false;break;}
  }
  Check(exact,"An instrumented partial Notes WM_PAINT changes only the explicit DIB clip, including its nonzero origin");
  std::cout<<"Normal Notes instrumented parent paints=3; empty native hidden-window regions="<<emptyNativeRegions
      <<"; explicit DIB clips supplied=3; visible USER update/compositor behavior excluded"<<std::endl;
}
static void NotesNormalSearchPaintStates(HWND owner,NotesNormalTrace &trace,unsigned &parentPaints) {
  auto &state=State(owner);const auto editor=state.attachedEditor;const auto editors=state.editors;
  const auto text=editor?NotesWindowText(editor):L"";
  const auto before=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes;
  for(const UINT command:{IDC_NOTES_FIND_TOGGLE,IDC_NOTES_REPLACE,IDC_NOTES_FIND_CLOSE}) {
    Command(owner,command);
    Check(state.findVisible==(command!=IDC_NOTES_FIND_CLOSE) && state.replaceVisible==(command==IDC_NOTES_REPLACE),"Production search commands establish Find-only, Find-and-Replace, and hidden search states");
    NotesNormalValidateTree(owner);trace.Reset();NotesNormalParentPaint(owner,trace,parentPaints);
    Check(state.attachedEditor==editor && state.editors==editors && (!editor || NotesWindowText(editor)==text) &&
        client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==before,"Search-frame presentation retains the same editor, exact document and saved notebook bytes");
    NotesNormalValidateTree(owner);
  }
}
static void NotesNormalOpening() {
  Check(backgroundNotesTests,"Normal-opening stress requires the existing strict background guards");
  g_bUiEnabled=true;g_iSelectedSessionTab=0;
  WritePrivateProfileStringW(L"notes_window",L"help_seen",L"1",g_sConfigPath.c_str());
  WritePrivateProfileStringW(L"notes_window",L"maximized",L"0",g_sConfigPath.c_str());
  WritePrivateProfileStringW(L"notes_window",L"width",L"960",g_sConfigPath.c_str());
  WritePrivateProfileStringW(L"notes_window",L"height",L"640",g_sConfigPath.c_str());
  g_hClientEdit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD,0,0,200,25,g_hGui,reinterpret_cast<HMENU>(IDC_CLIENT_EDIT),g_hInst,nullptr);
  g_hBtnNotes=CreateWindowExW(0,L"BUTTON",L"Notes",WS_CHILD|BS_PUSHBUTTON,0,28,100,25,g_hGui,reinterpret_cast<HMENU>(IDC_BTN_NOTES),g_hInst,nullptr);
  Check(g_hClientEdit && g_hBtnNotes && SetWindowSubclass(g_hGui,NotesNormalMainDispatch,0x4e4f524d,0),"A hidden native launcher command surface uses the production Notes command handler");
  LoadBakedThemesFromHeader();unsigned coldOpens=0,reopens=0,nativePrints=0,ownerDispatches=0,parentPaints=0;
  for(const wchar_t *themeName:{L"Dark - Gothic",L"Marine"}) {
    const auto theme=std::find_if(g_aCustomThemes.begin(),g_aCustomThemes.end(),[&](const ThemeEntry &entry){return entry.name==themeName;});
    Check(theme!=g_aCustomThemes.end(),"Normal opening uses a shipped dark or light theme");
    g_themeColors=theme->colors;g_bThemeIsDark=theme->dark;UpdateThemeBrushes();
    for(unsigned notebook=0;notebook<3;++notebook) for(const bool remembered:{false,true}) {
      const std::wstring name=L"Normal open "+std::to_wstring(coldOpens++);Fixture(name);
      const auto root=g_sDataDir/L"Sites"/name;
      if(notebook) {
        std::vector<client_notes::NotePage> pages;
        for(unsigned i=0;i<3;++i) {
          client_notes::NotePage page{L"Saved "+std::to_wstring(i+1),true,NotesPlainRtf(L"Remembered client note "+std::to_wstring(i+1)+L"\r\nSecond line"),{}};
          page.id=i+1;page.open=notebook==1;page.active=i==1;page.ticket=L"T20260921.0622";page.status=2;pages.push_back(std::move(page));
        }
        Check(client_notes::WriteNotebook(root,pages,client_notes::ReadNotebook(root)),"Normal opening starts with an independently saved multi-note or closed-tab notebook");
      }
      SetWindowTextW(g_hClientEdit,remembered ? L"" : name.c_str());
      WritePrivateProfileStringW(L"notes_window",L"last_client",remembered ? name.c_str() : L"Destination QA",g_sConfigPath.c_str());
      NotesNormalTrace trace;notesShowRequests.clear();NotesNormalValidateTree(g_hGui);
      SendMessageW(g_hGui,WM_COMMAND,MAKEWPARAM(IDC_BTN_NOTES,BN_CLICKED),reinterpret_cast<LPARAM>(g_hBtnNotes));
      HWND owner=g_hNotesWindow;
      Check(owner && IsWindow(owner) && trace.owner==owner,"The main Notes command creates its normal Notes window through native dispatch");
      auto &state=State(owner);const auto opening=trace.counts;
      const auto shows=std::count_if(notesShowRequests.begin(),notesShowRequests.end(),[&](const auto &request){return request.window==owner && request.command!=SW_HIDE;});
      const auto wrongShows=std::count_if(notesShowRequests.begin(),notesShowRequests.end(),[&](const auto &request){return request.window==owner && request.command!=SW_HIDE && (!request.registered || request.command!=SW_SHOW);});
      Check(shows==1 && !wrongShows,"An ordinary Notes open requests one normal show after native dialog initialization returns");
      Check(state.clientName==name && !IsZoomed(owner) && opening.init==1 && !state.loading && state.fontDpi==GetDpiForWindow(owner) && state.uiFont && state.tabFont,"Selected and remembered-client routes finish the correct normal notebook, theme fonts and layout before use");
      Check((notebook==2 && !HasActiveNote(state)) || (notebook!=2 && NotesEditorMatchesActive(state)),"Blank, multi-note and closed-tab startup prepare exactly the intended editor state");
      Check(!opening.paint,"Normal hidden startup does not force intermediate WM_PAINT frames during initialization");
      const HWND editor=state.attachedEditor;const auto page=state.attachedPageId;const auto editors=state.editors;
      const auto created=backgroundGuard.windowsCreated;RECT original{};GetWindowRect(owner,&original);
      const auto text=editor ? NotesWindowText(editor) : L"";const auto before=client_notes::ReadNotebook(root).notebook_snapshot.bytes;
      NotesNormalValidateTree(g_hGui);NotesNormalValidateTree(owner);trace.Reset();notesShowRequests.clear();
      NotesNormalObserveRepaints();
      for(unsigned iteration=0;iteration<64;++iteration) {
        SendMessageW(g_hGui,WM_COMMAND,MAKEWPARAM(IDC_BTN_NOTES,BN_CLICKED),reinterpret_cast<LPARAM>(g_hBtnNotes));++reopens;
      }
      RECT retained{};GetWindowRect(owner,&retained);
      NotesNormalCheckNoRepaints("Repeated ordinary Notes commands request no production invalidation or redraw of any owned window");
      Check(g_hNotesWindow==owner && state.attachedEditor==editor && state.attachedPageId==page && state.editors==editors && backgroundGuard.windowsCreated==created,"Repeated normal Notes commands retain the same window, editor and tab objects without re-creation");
      Check(EqualRect(&original,&retained) && NotesNormalQuiet(trace.counts) && !trace.counts.paint && !trace.counts.erase,"Repeated normal Notes opening does not move, resize, erase, stream or refresh the notebook");
      Check(!NotesNormalDirtyTree(g_hGui) && !NotesNormalDirtyTree(owner),"Repeated normal opens do not dirty the launcher, Notes or their native children");
      Check((!editor || NotesWindowText(editor)==text) && client_notes::ReadNotebook(root).notebook_snapshot.bytes==before,"Repeated ordinary opening preserves the editor text and exact saved notebook bytes");
      NotesNormalNativeChildren(owner,trace,nativePrints,ownerDispatches);NotesNormalParentPaint(owner,trace,parentPaints);
      NotesNormalValidateTree(g_hGui);NotesNormalValidateTree(owner);trace.Reset();
      NotesNormalParentPaint(owner,trace,parentPaints);
      Check(!NotesNormalDirtyTree(g_hGui) && !NotesNormalDirtyTree(owner),"Completed native Notes presentation leaves no parent or child repaint feedback");
      if(!remembered) NotesNormalSearchPaintStates(owner,trace,parentPaints);
      if(notebook==1) {
        Command(owner,IDC_NOTES_CLOSE_ALL);
        Check(!HasActiveNote(state) && !GetDlgItem(owner,IDC_CLIENT_NOTES_TEXT),"Closing all saved tabs removes the active editor before an empty parent repaint");
        NotesNormalParentPaint(owner,trace,parentPaints);
        if(!remembered) NotesNormalSearchPaintStates(owner,trace,parentPaints);
      }
      std::cout<<"Normal Notes case="<<(coldOpens-1)<<"; remembered="<<remembered<<"; notebook="<<notebook<<"; dark="<<g_bThemeIsDark
          <<"; initialization streams="<<opening.streamIn<<"/"<<opening.streamOut<<"; init geometry="<<opening.geometry
          <<"; init paints="<<opening.paint<<"; show requests="<<shows<<std::endl;
      DestroyWindow(owner);NotesBackgroundAudit("normal main-command opening, native printing and cleanup");
    }
  }
  std::cout<<"Normal Notes command cold opens="<<coldOpens<<"; retained normal reopens="<<reopens<<"; native child prints="<<nativePrints
      <<"; native owner-draw dispatches="<<ownerDispatches<<"; parent native paints="<<parentPaints<<std::endl;
  DestroyWindow(g_hClientEdit);g_hClientEdit=nullptr;DestroyWindow(g_hBtnNotes);g_hBtnNotes=nullptr;
  RemoveWindowSubclass(g_hGui,NotesNormalMainDispatch,0x4e4f524d);
}
