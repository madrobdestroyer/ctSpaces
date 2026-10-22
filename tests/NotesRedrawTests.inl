// Hidden native fixtures exercise the actual opening path and painted child
// surfaces. These checks cannot prove desktop compositor timing.
struct NotesPaintDib {
  static constexpr int width=224,height=84;
  HWND owner=nullptr;HDC reference=nullptr,dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;DWORD *pixels=nullptr;
  explicit NotesPaintDib(HWND owner) {
    this->owner=owner;
    reference=GetDC(owner);dc=CreateCompatibleDC(reference);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    bitmap=CreateDIBSection(reference,&info,DIB_RGB_COLORS,reinterpret_cast<void **>(&pixels),nullptr,0);
    if(!dc || !bitmap || !pixels) throw std::runtime_error("Notes offscreen paint fixture allocation failed");
    previous=SelectObject(dc,bitmap);
  }
  ~NotesPaintDib(){if(previous) SelectObject(dc,previous);if(bitmap) DeleteObject(bitmap);if(dc) DeleteDC(dc);if(reference) ReleaseDC(owner,reference);}
  void Clear(){GdiFlush();std::fill(pixels,pixels+width*height,0x005A27C3);}
  std::vector<DWORD> Read(){GdiFlush();std::vector<DWORD> result(pixels,pixels+width*height);for(auto &pixel:result) pixel&=0xffffff;return result;}
};
static bool NotesRedrawKnownItem(const DRAWITEMSTRUCT &draw,const ClientNotesDialogState &state,bool raw) {
#if !defined(CTSPACES_REDRAW_BASELINE)
  if(raw) return DrawNotesComboItemCore(draw) || DrawNotesTicketReferenceCore(draw) ||
      DrawNotesWorkStatusCore(draw,state) || DrawNotesToolbarButtonCore(draw,state);
#endif
  (void)raw;
  return DrawNotesComboItem(draw) || DrawNotesTicketReference(draw) ||
      DrawNotesWorkStatus(draw,state) || DrawNotesToolbarButton(draw,state);
}
static void NotesRedrawOpening() {
  Check(backgroundNotesTests,"Opening/redraw regression requires the hidden guarded scope");
  g_bUiEnabled=true;
  WritePrivateProfileStringW(L"notes_window",L"help_seen",L"1",g_sConfigPath.c_str());
  WritePrivateProfileStringW(L"notes_window",L"width",L"960",g_sConfigPath.c_str());
  WritePrivateProfileStringW(L"notes_window",L"height",L"640",g_sConfigPath.c_str());
  bool openingOrder=true,openingMode=true,stableWindow=true,atomicPaint=true,pixelsEqual=true,fallbackEqual=true,eraseQuiet=true;
  unsigned childCases=0,comboCases=0,differentCases=0;
  for(const bool maximized:{false,true}) {
    WritePrivateProfileStringW(L"notes_window",L"maximized",maximized ? L"1" : L"0",g_sConfigPath.c_str());
    notesShowRequests.clear();ShowClientNotes(L"Workspace QA");
    HWND owner=g_hNotesWindow;Check(owner && IsWindow(owner),"The production Notes entry point creates its hidden fixture");
    unsigned visible=0,early=0;int lastShow=SW_HIDE;
    for(const auto &request:notesShowRequests) if(request.window==owner && request.command!=SW_HIDE) {
      ++visible;lastShow=request.command;if(!request.registered) ++early;
    }
    std::cout<<"Notes open mode="<<(maximized ? "maximized" : "normal")<<"; visible requests="<<visible
        <<"; requests before CreateDialog returned="<<early<<"; final show command="<<lastShow<<std::endl;
    openingOrder=openingOrder && !early && visible==1;
    openingMode=openingMode && lastShow==(maximized ? SW_MAXIMIZE : SW_SHOW);
    const auto beforeWindows=backgroundGuard.windowsCreated;
    const HWND editor=State(owner).attachedEditor;const uint64_t page=State(owner).attachedPageId;
    notesShowRequests.clear();ShowClientNotes(L"Workspace QA");
    stableWindow=stableWindow && owner==g_hNotesWindow && editor==State(owner).attachedEditor &&
        page==State(owner).attachedPageId && beforeWindows==backgroundGuard.windowsCreated;

    if(!maximized) {
      auto &state=State(owner);
      if(HasActiveNote(state)) {
        auto &page=state.pages[state.activePage];page.status=2;page.ticket=L"T20260921.0622";
        page.ticketUrl=L"https://example.invalid/ticket";page.tags={L"printing"};
        UpdateNotesToolbar(owner,state);LayoutClientNotes(owner,GetDpiForWindow(owner));
      }
      LoadBakedThemesFromHeader();
      NotesPaintDib target(g_hGui);
      for(const wchar_t *themeName:{L"Dark - Gothic",L"Marine"}) {
        const auto theme=std::find_if(g_aCustomThemes.begin(),g_aCustomThemes.end(),[&](const ThemeEntry &entry){return entry.name==themeName;});
        Check(theme!=g_aCustomThemes.end(),"Redraw coverage uses a shipped dark or light palette");
        g_themeColors=theme->colors;g_bThemeIsDark=theme->dark;UpdateThemeBrushes();
        for(const int id:{IDC_NOTES_SWITCH_CLIENT,IDC_NOTES_LIBRARY,IDC_NOTES_TRASH,IDC_NOTES_MORE,IDC_NOTES_HELP,
            IDC_NOTES_FIND_TOGGLE,IDC_NOTES_SNIPPETS,IDC_NOTES_TEMPLATES,IDC_NOTES_DETAILS,IDC_NOTES_OPEN_TICKET,
            IDC_NOTES_WORKLOG,IDC_NOTES_FORMAT,IDC_NOTES_ADD_TAB,IDC_NOTES_BOLD,IDC_NOTES_HIGHLIGHT_COLOR,
            IDC_NOTES_TICKET_REFERENCE,IDC_NOTES_WORK_STATUS,IDC_NOTES_STYLE,IDC_NOTES_SIZE}) {
          for(const UINT appearance:{0u,static_cast<UINT>(ODS_HOTLIGHT),static_cast<UINT>(ODS_DISABLED)}) {
            for(const bool offset:{false,true}) {
              DRAWITEMSTRUCT draw{};draw.CtlID=id;draw.hwndItem=GetDlgItem(owner,id);draw.hDC=target.dc;
              // Button/static owner-draw coordinates belong to their actual
              // client area. An oversized invented rectangle leaves corners
              // outside that client untouched in the original renderer.
              GetClientRect(draw.hwndItem,&draw.rcItem);
              draw.rcItem.right=(std::min)(draw.rcItem.right,static_cast<LONG>(NotesPaintDib::width));
              draw.rcItem.bottom=(std::min)(draw.rcItem.bottom,static_cast<LONG>(NotesPaintDib::height));
              if(offset) {
                draw.rcItem.left=(std::min)(4L,draw.rcItem.right/4);
                draw.rcItem.top=(std::min)(3L,draw.rcItem.bottom/4);
              }
              draw.itemState=appearance;
              draw.CtlType=id==IDC_NOTES_STYLE || id==IDC_NOTES_SIZE ? ODT_COMBOBOX :
                  id==IDC_NOTES_TICKET_REFERENCE || id==IDC_NOTES_WORK_STATUS ? ODT_STATIC : ODT_BUTTON;
              draw.itemID=0;
              if(offset && draw.CtlType==ODT_COMBOBOX) {
                // Dropdown item rows legitimately have a nonzero Y origin,
                // including rows below the closed combo's own client area.
                const LONG rowHeight=draw.rcItem.bottom;draw.rcItem.top=45;
                draw.rcItem.bottom=(std::min)(static_cast<LONG>(NotesPaintDib::height),45+rowHeight);
              }
              target.Clear();const int saved=SaveDC(target.dc);IntersectClipRect(target.dc,draw.rcItem.left,draw.rcItem.top,draw.rcItem.right,draw.rcItem.bottom);
              const bool rawHandled=NotesRedrawKnownItem(draw,state,true);RestoreDC(target.dc,saved);
              const auto expected=target.Read();
              target.Clear();const auto erased=target.Read();
              SendMessageW(draw.hwndItem,WM_ERASEBKGND,reinterpret_cast<WPARAM>(target.dc),0);
              eraseQuiet=eraseQuiet && erased==target.Read();
              target.Clear();observedPaintTarget=target.dc;observedPaintPublishes=0;
              const bool handled=NotesRedrawKnownItem(draw,state,false);
              observedPaintTarget=nullptr;
              atomicPaint=atomicPaint && rawHandled && handled && observedPaintPublishes==1;
              const auto actual=target.Read();
              if(expected!=actual) {
                if(differentCases++<8) {
                  const auto mismatch=std::mismatch(expected.begin(),expected.end(),actual.begin());
                  const auto at=static_cast<unsigned>(mismatch.first-expected.begin());RECT native{};GetClientRect(draw.hwndItem,&native);
                  std::cout<<"Paint mismatch id="<<id<<"; dark="<<g_bThemeIsDark<<"; appearance="<<appearance<<"; offset="<<offset
                      <<"; native="<<native.right<<"x"<<native.bottom<<"; requested="<<draw.rcItem.left<<","<<draw.rcItem.top<<","<<draw.rcItem.right<<","<<draw.rcItem.bottom
                      <<"; first="<<(at%NotesPaintDib::width)<<","<<(at/NotesPaintDib::width)<<"; expected="<<std::hex<<*mismatch.first<<"; actual="<<*mismatch.second<<std::dec<<std::endl;
                }
                pixelsEqual=false;
              }
#if !defined(CTSPACES_REDRAW_BASELINE)
              for(bool *fault:{&failNextPaintBitmap,&failNextPaintSelection,&failNextPaintViewport}) {
                target.Clear();*fault=true;observedPaintTarget=target.dc;observedPaintPublishes=0;
                const bool fallbackHandled=NotesRedrawKnownItem(draw,state,false);
                observedPaintTarget=nullptr;
                fallbackEqual=fallbackEqual && fallbackHandled && !*fault && !observedPaintPublishes && expected==target.Read();
              }
#endif
              if(!offset && !appearance && (id==IDC_NOTES_SWITCH_CLIENT || id==IDC_NOTES_MORE || id==IDC_NOTES_DETAILS || id==IDC_NOTES_STYLE)) {
                const fs::path captureRoot=fs::current_path()/L"build/redraw-regression-2026-10-15/notes-repaint/rendered";
                fs::create_directories(captureRoot);GetClientRect(draw.hwndItem,&draw.rcItem);
                draw.rcItem.right=(std::min)(draw.rcItem.right,static_cast<LONG>(NotesPaintDib::width));
                draw.rcItem.bottom=(std::min)(draw.rcItem.bottom,static_cast<LONG>(NotesPaintDib::height));
                target.Clear();NotesRedrawKnownItem(draw,state,false);GdiFlush();
                Gdiplus::Bitmap source(target.bitmap,nullptr);
                std::unique_ptr<Gdiplus::Bitmap> cropped(source.Clone(0,0,draw.rcItem.right,draw.rcItem.bottom,PixelFormat32bppARGB));
                const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
                const auto name=std::wstring(g_bThemeIsDark ? L"gothic-" : L"marine-")+std::to_wstring(id)+
#if defined(CTSPACES_REDRAW_BASELINE)
                    L"-before.png";
#else
                    L"-after.png";
#endif
                Check(cropped && cropped->Save((captureRoot/name).c_str(),&png,nullptr)==Gdiplus::Ok,"Save representative offscreen production-rendered child pixels");
              }
              ++childCases;
            }
          }
        }
        for(const int id:{IDC_NOTES_STYLE,IDC_NOTES_SIZE}) {
          target.Clear();observedPaintTarget=target.dc;observedPaintPublishes=0;
          SendDlgItemMessageW(owner,id,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(target.dc),PRF_CLIENT);
          observedPaintTarget=nullptr;atomicPaint=atomicPaint && observedPaintPublishes==1;++comboCases;
        }
      }
      DRAWITEMSTRUCT unknown{};unknown.CtlType=ODT_BUTTON;unknown.CtlID=99999;unknown.hwndItem=GetDlgItem(owner,IDC_NOTES_LIBRARY);
      unknown.hDC=target.dc;unknown.rcItem={3,4,200,70};target.Clear();const auto untouched=target.Read();
      observedPaintTarget=target.dc;observedPaintPublishes=0;
      const bool unknownHandled=NotesRedrawKnownItem(unknown,state,false);observedPaintTarget=nullptr;
      Check(!unknownHandled && !observedPaintPublishes && untouched==target.Read(),"Unknown owner-draw items never publish an uninitialized buffer");
      ValidateRect(g_hGui,nullptr);ValidateRect(owner,nullptr);
      EnumChildWindows(owner,[](HWND control,LPARAM)->BOOL {ValidateRect(control,nullptr);return TRUE;},0);
      for(unsigned iteration=0;iteration<100;++iteration) {
        DRAWITEMSTRUCT item{};item.CtlType=ODT_BUTTON;item.CtlID=IDC_NOTES_MORE;item.hwndItem=GetDlgItem(owner,item.CtlID);
        item.hDC=target.dc;GetClientRect(item.hwndItem,&item.rcItem);
        SendMessageW(owner,WM_DRAWITEM,item.CtlID,reinterpret_cast<LPARAM>(&item));
        SendDlgItemMessageW(owner,IDC_NOTES_STYLE,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(target.dc),PRF_CLIENT);
      }
      unsigned dirty=GetUpdateRect(g_hGui,nullptr,FALSE)!=FALSE;
      dirty+=GetUpdateRect(owner,nullptr,FALSE)!=FALSE;
      EnumChildWindows(owner,[](HWND control,LPARAM data)->BOOL {
        if(GetUpdateRect(control,nullptr,FALSE)) ++*reinterpret_cast<unsigned *>(data);return TRUE;
      },reinterpret_cast<LPARAM>(&dirty));
      Check(dirty==0,"Repeated native owner drawing and combo painting schedule no feedback repaints in Notes or launcher");
    }
    DestroyWindow(owner);NotesBackgroundAudit(maximized ? "maximized Notes opening" : "normal Notes opening and buffered paint");
  }
  std::cout<<"Notes child paint cases="<<childCases<<"; different cases="<<differentCases<<"; combo surface cases="<<comboCases<<"; single publish="<<atomicPaint
      <<"; renderer pixel equality="<<pixelsEqual<<"; allocation fallback equality="<<fallbackEqual
      <<"; child erase quiet="<<eraseQuiet
      <<"; opening order="<<openingOrder<<"; remembered opening mode="<<openingMode<<std::endl;
  Check(openingOrder,"Notes never shows a normal or maximized window before initialization completes");
  Check(openingMode,"The sole opening show request preserves the remembered normal/maximized preference");
  Check(stableWindow,"Reopening existing Notes keeps the same window and rich editor without re-creation");
  Check(atomicPaint,"Supported owner-drawn Notes children and closed format combos publish one finished frame");
  Check(eraseQuiet,"Opaque custom Notes children acknowledge native erase without clearing their displayed pixels");
  Check(pixelsEqual,"Buffered child painting preserves the existing pixels, including nonzero item rectangles");
  Check(fallbackEqual,"A failed paint bitmap allocation renders the same clipped pixels directly without garbage publication");
}
