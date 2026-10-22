// Real native Button WM_PRINTCLIENT dispatch after normal client switches and
// resizes. Every pixel target is a process-private DIB; no desktop capture,
// device input, visibility or physical focus is needed.
namespace notes_plus_switch {
struct Dib {
  HWND owner;int width,height;HDC reference,dc;HBITMAP bitmap;HGDIOBJ previous;DWORD *pixels=nullptr;
  explicit Dib(HWND window):owner(window){
    RECT area{};GetClientRect(window,&area);width=area.right;height=area.bottom;
    reference=GetDC(window);dc=CreateCompatibleDC(reference);BITMAPINFO info{};
    info.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB};
    bitmap=CreateDIBSection(reference,&info,DIB_RGB_COLORS,reinterpret_cast<void **>(&pixels),nullptr,0);
    if(!dc||!bitmap||!pixels||width<=0||height<=0)throw std::runtime_error("Plus-switch private DIB allocation failed");
    previous=SelectObject(dc,bitmap);
  }
  ~Dib(){SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(owner,reference);}
  void Clear(){GdiFlush();std::fill(pixels,pixels+static_cast<size_t>(width)*height,0x005A27C3);}
  std::vector<DWORD> Read(){GdiFlush();std::vector<DWORD> result(pixels,pixels+static_cast<size_t>(width)*height);for(auto &value:result)value&=0xffffff;return result;}
  void Save(const fs::path &path){
    fs::create_directories(path.parent_path());GdiFlush();
    Gdiplus::Bitmap image(width,height,width*4,PixelFormat32bppRGB,reinterpret_cast<BYTE *>(pixels));
    const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
    if(image.Save(path.c_str(),&png,nullptr)!=Gdiplus::Ok)throw std::runtime_error("Plus-switch native DIB capture failed");
  }
};
struct NativeDraw {
  HWND button=nullptr;unsigned draws=0;DRAWITEMSTRUCT item{};RECT clip{};POINT viewport{};int clipKind=ERROR;
};
static LRESULT CALLBACK Observe(HWND owner,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){
  auto &trace=*reinterpret_cast<NativeDraw *>(data);
  if(message==WM_DRAWITEM&&l){const auto &item=*reinterpret_cast<const DRAWITEMSTRUCT *>(l);
    if(item.hwndItem==trace.button){++trace.draws;trace.item=item;trace.clipKind=GetClipBox(item.hDC,&trace.clip);GetViewportOrgEx(item.hDC,&trace.viewport);}
  }
  if(message==WM_NCDESTROY)RemoveWindowSubclass(owner,Observe,id);
  return DefSubclassProc(owner,message,w,l);
}
static unsigned checks=0,failures=0,prints=0,defaultProbes=0;
static void Verify(bool passed,const char *description){++checks;if(!passed)++failures;std::cout<<(passed?"PASS ":"FAIL ")<<description<<'\n';}
static std::string Rectangle(const RECT &area){return std::to_string(area.left)+","+std::to_string(area.top)+","+std::to_string(area.right)+","+std::to_string(area.bottom);}
static void Print(HWND owner,NativeDraw &trace,const fs::path &capture,const char *phase,bool strict){
  auto &state=State(owner);const auto button=trace.button;RECT area{};GetClientRect(button,&area);
  Dib actual(button),reference(button);actual.Clear();reference.Clear();trace.draws=0;trace.item={};trace.clip={};trace.viewport={};trace.clipKind=ERROR;
  const auto type=GetWindowLongPtrW(button,GWL_STYLE)&BS_TYPEMASK;
  const auto ui=SendMessageW(button,WM_QUERYUISTATE,0,0),defaultId=SendMessageW(owner,DM_GETDEFID,0,0);
  observedPaintTarget=actual.dc;observedPaintPublishes=0;notesObservedInvalidates=notesObservedRedraws=0;notesObserveRepaintRequests=true;
  SendMessageW(button,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(actual.dc),PRF_CLIENT);
  notesObserveRepaintRequests=false;observedPaintTarget=nullptr;++prints;const auto pixels=actual.Read();
  DRAWITEMSTRUCT expected{ODT_BUTTON,IDC_NOTES_ADD_TAB,0,ODA_DRAWENTIRE,0,button,reference.dc,area,0};
  if(trace.draws)expected=trace.item;expected.hDC=reference.dc;
  Check(DrawNotesToolbarButtonCore(expected,state),"An independent unbuffered reference handles the plus button's real native owner-draw state");
  const auto wanted=reference.Read();
  const std::array<size_t,4> edges={static_cast<size_t>(area.bottom/2)*area.right,
    static_cast<size_t>(area.bottom/2)*area.right+area.right-1,static_cast<size_t>(area.right/2),
    static_cast<size_t>(area.bottom-1)*area.right+area.right/2};
  const auto background=(GetRValue(NotesWindowColor())<<16)|(GetGValue(NotesWindowColor())<<8)|GetBValue(NotesWindowColor());
  Check(area.right>=20&&area.bottom>=20,"The native plus surface has room for twenty central samples on each border");
  bool edgeInk=true;unsigned edgeSamples=0;
  for(int offset=-10;offset<10;++offset){
    const int x=area.right/2+offset,y=area.bottom/2+offset;
    const std::array<size_t,4> points={static_cast<size_t>(y)*area.right,
      static_cast<size_t>(y)*area.right+area.right-1,static_cast<size_t>(x),
      static_cast<size_t>(area.bottom-1)*area.right+x};
    for(const auto at:points){++edgeSamples;edgeInk=edgeInk&&pixels[at]==wanted[at]&&wanted[at]!=0x005A27C3&&wanted[at]!=background;}
  }
  std::cout<<"Plus native print phase="<<phase<<" strict="<<strict<<" client="<<State(owner).visiblePages.size()<<" dpi="<<GetDpiForWindow(button)
      <<" type="<<type<<" default="<<LOWORD(defaultId)<<" default-present="<<(HIWORD(defaultId)==DC_HASDEFID)<<" ui="<<ui<<" draws="<<trace.draws
      <<" publishes="<<observedPaintPublishes<<" item="<<Rectangle(trace.item.rcItem)<<" clip-kind="<<trace.clipKind<<" clip="<<Rectangle(trace.clip)
      <<" viewport="<<trace.viewport.x<<','<<trace.viewport.y<<" exact="<<(pixels==wanted)<<" four-edges="<<edgeInk<<" edge-samples="<<edgeSamples
      <<" edge-RGB="<<std::hex<<pixels[edges[0]]<<','<<pixels[edges[1]]<<','<<pixels[edges[2]]<<','<<pixels[edges[3]]<<std::dec<<'\n';
  if(strict){
    Verify(type==BS_OWNERDRAW&&trace.draws==1&&observedPaintPublishes==1,"Normal switched plus buttons retain native owner-draw style and publish exactly one complete surface");
    Verify(EqualRect(&trace.item.rcItem,&area)&&trace.clipKind==SIMPLEREGION&&EqualRect(&trace.clip,&area)&&!trace.viewport.x&&!trace.viewport.y,"Native Button owner-draw uses its entire exact client rectangle and unclipped zero-origin DIB target");
    Verify(pixels==wanted&&std::find(pixels.begin(),pixels.end(),0x005A27C3)==pixels.end(),"The actual switched Button print matches the full unbuffered reference without stale or unpainted pixels");
    Verify(edgeInk,"All four switched plus-button border edges contain the expected visible theme outline");
    Verify(!notesObservedInvalidates&&!notesObservedRedraws,"Native switched plus printing schedules no production invalidation or redraw feedback");
  }else ++defaultProbes;
  actual.Save(capture);
}
static bool Run(const fs::path &captures){
  checks=failures=prints=defaultProbes=0;
  const auto allowed=fs::weakly_canonical(fs::current_path()/L"build").wstring()+L"\\";
  const auto output=fs::weakly_canonical(fs::absolute(captures));
  Check(output.wstring().size()>allowed.size()&&!_wcsnicmp(output.c_str(),allowed.c_str(),allowed.size()),"Plus-switch native captures remain in private workspace build evidence");
  const std::array<std::wstring,2> names={L"Plus Northstar thirteen",L"Plus Southstar three"};
  for(size_t client=0;client<names.size();++client){
    Fixture(names[client]);const auto root=g_sDataDir/L"Sites"/names[client];std::vector<client_notes::NotePage> pages;
    for(unsigned note=0;note<(client?3u:13u);++note){client_notes::NotePage page{L"Note "+std::to_wstring(note+1),true,NotesPlainRtf(L"Private client sentinel "+names[client]+L" page "+std::to_wstring(note+1)),{}};page.id=note+1;page.active=note==0;pages.push_back(std::move(page));}
    Check(client_notes::WriteNotebook(root,pages,client_notes::ReadNotebook(root)),"The independent plus-switch fixture saves its exact thirteen-tab or three-tab client");
  }
  const auto originalColors=g_themeColors;const bool originalDark=g_bThemeIsDark;const auto originalHot=g_hHotButton;
  LoadBakedThemesFromHeader();unsigned themeIndex=0;
  for(const auto *themeName:{L"Dark - Gothic",L"Marine"}){
    const auto theme=std::find_if(g_aCustomThemes.begin(),g_aCustomThemes.end(),[&](const auto &entry){return entry.name==themeName;});
    Check(theme!=g_aCustomThemes.end(),"The plus-switch proof uses an actual shipped dark or light theme");g_themeColors=theme->colors;g_bThemeIsDark=theme->dark;UpdateThemeBrushes();
    HWND owner=Open(names[0]);auto &state=State(owner);const auto button=GetDlgItem(owner,IDC_NOTES_ADD_TAB);
    // Establish normal saved/view baselines before testing presentation-only work.
    Save(owner);Check(SwitchClientNotes(owner,state,names[1],g_sDataDir/L"Sites"/names[1]),"The plus fixture canonically opens its separate three-tab client");Save(owner);
    Check(SwitchClientNotes(owner,state,names[0],g_sDataDir/L"Sites"/names[0]),"The plus fixture returns through the ordinary client-switch path");Save(owner);
    std::array<std::string,2> durable;for(size_t client=0;client<names.size();++client)durable[client]=client_notes::ReadNotebook(g_sDataDir/L"Sites"/names[client]).notebook_snapshot.bytes;
    NativeDraw trace;trace.button=button;Check(SetWindowSubclass(owner,Observe,0x50535749,reinterpret_cast<DWORD_PTR>(&trace))!=FALSE,"The plus proof observes only its process-owned native owner-draw dispatch");
    for(unsigned turn=0;turn<4;++turn){
      const unsigned client=turn%2;
      if(turn)Check(SwitchClientNotes(owner,state,names[client],g_sDataDir/L"Sites"/names[client]),"The live-shaped thirteen/three transition uses the real production client switch");
      Verify(g_hNotesWindow==owner&&GetDlgItem(owner,IDC_NOTES_ADD_TAB)==button&&state.visiblePages.size()==(client?3u:13u),"Client switching retains the same Notes window and plus control with the exact target tab count");
      const auto edit=state.attachedEditor;std::string rtf;Check(StreamClientNotesOut(edit,rtf),"The plus-switch proof snapshots exact active rich text");CHARRANGE selection{};NotesSelection(edit,selection);const auto undo=SendMessageW(edit,EM_CANUNDO,0,0);
      for(unsigned size=0;size<3;++size){
        SetWindowPos(owner,nullptr,0,0,size==1?1200:905,size==1?720:545,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);LayoutClientNotes(owner,GetDpiForWindow(owner));
        for(unsigned phase=0;phase<3;++phase){g_hHotButton=phase==1?button:nullptr;
          const auto file=L"theme"+std::to_wstring(themeIndex)+L"-turn"+std::to_wstring(turn)+L"-size"+std::to_wstring(size)+L"-state"+std::to_wstring(phase)+L".png";
          Print(owner,trace,output/file,phase==1?"hot":phase==2?"rest-after-hot":"rest",true);
        }
        std::string after;Check(StreamClientNotesOut(edit,after),"The plus-switch proof reads the active document after its native render/resize cycle");CHARRANGE current{};NotesSelection(edit,current);
        Verify(state.attachedEditor==edit&&rtf==after&&selection.cpMin==current.cpMin&&selection.cpMax==current.cpMax&&SendMessageW(edit,EM_CANUNDO,0,0)==undo,"Client-preserving native resize and plus repaint retain exact RTF, selection, editor identity and Undo availability");
      }
      for(size_t peer=0;peer<names.size();++peer)Verify(client_notes::ReadNotebook(g_sDataDir/L"Sites"/names[peer]).notebook_snapshot.bytes==durable[peer],"Switching and rendering plus controls preserve both clients' exact saved notebook bytes");
    }
    // This is an isolated diagnostic, not physical focus and not evidence by
    // itself that normal switching requests a default-button style change.
    const auto defaultId=SendMessageW(owner,DM_GETDEFID,0,0),ui=SendMessageW(owner,WM_QUERYUISTATE,0,0);
    g_hHotButton=nullptr;SendMessageW(owner,DM_SETDEFID,IDC_NOTES_ADD_TAB,0);SendMessageW(owner,WM_UPDATEUISTATE,MAKEWPARAM(UIS_CLEAR,UISF_HIDEFOCUS|UISF_HIDEACCEL),0);
    Print(owner,trace,output/(L"theme"+std::to_wstring(themeIndex)+L"-default-id.png"),"default-id-diagnostic",false);
    SendMessageW(owner,DM_SETDEFID,HIWORD(defaultId)==DC_HASDEFID?LOWORD(defaultId):0,0);
    SendMessageW(owner,WM_UPDATEUISTATE,MAKEWPARAM(UIS_SET,ui&(UISF_HIDEFOCUS|UISF_HIDEACCEL)),0);
    SendMessageW(owner,WM_UPDATEUISTATE,MAKEWPARAM(UIS_CLEAR,(~ui)&(UISF_HIDEFOCUS|UISF_HIDEACCEL)),0);
    Print(owner,trace,output/(L"theme"+std::to_wstring(themeIndex)+L"-restored-default-id.png"),"restored-default-id-diagnostic",false);
    RemoveWindowSubclass(owner,Observe,0x50535749);DestroyWindow(owner);NotesBackgroundAudit("native plus switch, resize, hot/rest and isolated default-ID diagnostics");++themeIndex;
  }
  g_hHotButton=originalHot;g_themeColors=originalColors;g_bThemeIsDark=originalDark;UpdateThemeBrushes();
  std::cout<<"Notes plus switch native proof: "<<checks<<" checks, "<<prints<<" actual Button prints, "<<defaultProbes<<" isolated default-ID diagnostics, "<<failures<<" failures; physical focus/compositor excluded.\n";
  return failures==0;
}
}
