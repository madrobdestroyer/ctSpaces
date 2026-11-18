// Native layout/style behavior under the existing process-private UI guards.
// These checks use owned controls and memory DIBs, never screen capture/input.
#include <oleacc.h>
#pragma comment(lib,"Oleacc.lib")
namespace notes_polish {
static std::set<std::pair<bool,UINT>> baselineCoverage;
static RECT Bounds(HWND child,HWND relative=nullptr) {
  RECT value{};Check(child && GetWindowRect(child,&value),"Polish geometry reads an existing owned control");
  if(relative) MapWindowPoints(nullptr,relative,reinterpret_cast<POINT *>(&value),2);
  return value;
}
static bool Contains(const RECT &outer,const RECT &inner) {
  return inner.right>inner.left && inner.bottom>inner.top && inner.left>=outer.left &&
      inner.top>=outer.top && inner.right<=outer.right && inner.bottom<=outer.bottom;
}
static bool VisibleRequest(HWND child) {
  return child && (GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE)!=0;
}
static POINT TextOrigin(HWND owner) {
  POINT first{};const HWND edit=Edit(owner);
  SendMessageW(edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&first),0);
  MapWindowPoints(edit,owner,&first,1);return first;
}
static void SameSelection(HWND edit,const CHARRANGE &before,const char *message) {
  CHARRANGE after{};NotesSelection(edit,after);
  Check(after.cpMin==before.cpMin && after.cpMax==before.cpMax,message);
}
static void StableAccessibleNames(HWND owner) {
  auto &state=State(owner);const HWND editor=Edit(owner),tabs=GetDlgItem(owner,IDC_NOTES_TABS);
  const auto text=NotesWindowText(editor);CHARRANGE selected{};NotesSelection(editor,selected);
  const auto undo=SendMessageW(editor,EM_GETUNDONAME,0,0),redo=SendMessageW(editor,EM_GETREDONAME,0,0);
  struct Accessible {
    IAccessible *value=nullptr;
    explicit Accessible(HWND window){Check(SUCCEEDED(AccessibleObjectFromWindow(window,OBJID_CLIENT,IID_IAccessible,reinterpret_cast<void **>(&value))) && value,"Notes keeps an actual native accessible control object");}
    ~Accessible(){if(value)value->Release();}
    std::wstring Name(LONG child=CHILDID_SELF) const {
      VARIANT target{};target.vt=VT_I4;target.lVal=child;BSTR raw=nullptr;
      const auto result=value->get_accName(target,&raw);std::wstring name=raw?std::wstring(raw,SysStringLen(raw)):std::wstring{};SysFreeString(raw);
      Check(SUCCEEDED(result),"A native Notes accessible name can be read without replacing its provider");return name;
    }
    LONG Role() const {
      VARIANT target{},role{};target.vt=VT_I4;target.lVal=CHILDID_SELF;
      const auto result=value->get_accRole(target,&role);const LONG nativeRole=role.vt==VT_I4?role.lVal:0;VariantClear(&role);
      Check(SUCCEEDED(result) && nativeRole,"Notes retains its native accessibility role");return nativeRole;
    }
  } group(tabs),document(editor);
  Check(group.Name()==L"Open notes" && group.Role()==ROLE_SYSTEM_PAGETABLIST,
      "The native tab group stays named Open notes when neighboring ticket, tags or status text changes");
  Check(document.Name()==L"Note content" && document.Role()==ROLE_SYSTEM_TEXT,
      "The native rich editor stays named Note content rather than inheriting metadata or a save/details warning");
  LONG children=0;Check(SUCCEEDED(group.value->get_accChildCount(&children)) && children>=TabCtrl_GetItemCount(tabs),
      "The named tab group still exposes each native note item");
  for(size_t i=0;i<state.visiblePages.size();++i)
    Check(group.Name(static_cast<LONG>(i)+1)==state.pages[state.visiblePages[i]].name,
        "Naming the tab group leaves every accessible tab item's exact note title intact");
  SameSelection(editor,selected,"Accessible-name queries preserve the exact native document selection");
  Check(NotesWindowText(editor)==text && SendMessageW(editor,EM_GETUNDONAME,0,0)==undo && SendMessageW(editor,EM_GETREDONAME,0,0)==redo,
      "Stable accessibility labels never become document text or alter native Undo/Redo history");
}
static COLORREF NativeRangeInk(HWND edit,LONG first,LONG last) {
  IUnknown *ole=nullptr;ITextDocument *document=nullptr;ITextRange *range=nullptr;ITextFont *font=nullptr;LONG color=0;
  SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(&ole));
  if(ole){ole->QueryInterface(__uuidof(ITextDocument),reinterpret_cast<void **>(&document));ole->Release();}
  const bool read=document && SUCCEEDED(document->Range(first,last,&range)) && SUCCEEDED(range->GetFont(&font)) && SUCCEEDED(font->GetForeColor(&color));
  if(font)font->Release();if(range)range->Release();if(document)document->Release();
  Check(read,"A native TOM range exposes glyph ink without moving the initial insertion caret");return static_cast<COLORREF>(color);
}
static void ContextTypingInk(HWND owner) {
  auto &state=State(owner);Command(owner,IDC_NOTES_ADD_TAB);const HWND edit=Edit(owner);
  Type(owner,L"highlight\r\nlink\r\nchecked");Select(edit,0,9);
  CHARFORMAT2W highlight{sizeof(highlight)};highlight.dwMask=CFM_COLOR|CFM_BACKCOLOR;highlight.crBackColor=RGB(255,235,96);highlight.crTextColor=RGB(24,24,24);
  SendMessageW(edit,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&highlight));
  Select(edit,10,14);inputs.push_back({L"link",L"https://example.test/caret-link"});Command(owner,IDC_NOTES_LINK);
  Select(edit,15,15);ToggleNotesChecklist(edit,true);Save(owner);KillTimer(owner,1);
  const auto durable=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes;const auto text=NotesWindowText(edit);
  for(const LONG position:{2L,11L,19L}) {
    Select(edit,position,position);CHARFORMAT2W before{sizeof(before)},after{sizeof(after)};
    SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&before));
    const auto undo=SendMessageW(edit,EM_GETUNDONAME,0,0),redo=SendMessageW(edit,EM_GETREDONAME,0,0),modified=SendMessageW(edit,EM_GETMODIFY,0,0);
    CHARRANGE selected{};NotesSelection(edit,selected);std::string original;Check(StreamClientNotesOut(edit,original),"Context typing retains exact native RTF before a theme refresh");
    const auto address=NotesNativeLinkAddress(edit,state,{10,14});RefreshNotesForTheme();KillTimer(owner,1);
    SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&after));
    const DWORD effects=CFE_BOLD|CFE_ITALIC|CFE_UNDERLINE|CFE_STRIKEOUT|CFE_LINK|CFE_LINKPROTECTED|CFE_AUTOCOLOR|CFE_AUTOBACKCOLOR;
    Check((after.dwEffects&effects)==(before.dwEffects&effects) && after.crTextColor==before.crTextColor &&
        after.crBackColor==before.crBackColor && after.yHeight==before.yHeight && !wcscmp(after.szFaceName,before.szFaceName),
        "The insertion-ink repair preserves highlighted, linked and checked-text typing formats instead of forcing ordinary prose colors");
    SameSelection(edit,selected,"Refreshing contextual typing ink preserves the collapsed native selection");
    std::string current;Check(StreamClientNotesOut(edit,current) && current==original && NotesWindowText(edit)==text &&
        SendMessageW(edit,EM_GETUNDONAME,0,0)==undo && SendMessageW(edit,EM_GETREDONAME,0,0)==redo && SendMessageW(edit,EM_GETMODIFY,0,0)==modified &&
        address==L"https://example.test/caret-link" && NotesNativeLinkAddress(edit,state,{10,14})==address,
        "Contextual recoloring preserves native RTF, Undo/Redo, modification state and the exact link target");
  }
  Check(client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==durable,"Contextual caret theme refresh never publishes or changes the saved client notebook");
}
static void TemplateInitialTypingInk() {
  struct RestoreTheme {
    ThemeColors colors;bool dark;
    ~RestoreTheme(){g_themeColors=colors;g_bThemeIsDark=dark;UpdateThemeBrushes();}
  } restore{g_themeColors,g_bThemeIsDark};
  LoadBakedThemesFromHeader();
  for(const wchar_t *name:{L"Dark - Gothic",L"Marine"}) {
    const auto theme=std::find_if(g_aCustomThemes.begin(),g_aCustomThemes.end(),[&](const auto &entry){return entry.name==name;});
    Check(theme!=g_aCustomThemes.end(),"Initial template typing uses real dark and light app themes");
    g_themeColors=theme->colors;g_bThemeIsDark=theme->dark;UpdateThemeBrushes();
    const auto client=g_bThemeIsDark?L"Dark template initial ink QA":L"Light template initial ink QA";Fixture(client);const HWND owner=Open(client);
    auto &state=State(owner);auto page=NotesTemplateFromPage(NotesBuiltinTemplates().front());
    Check(NotesAddPage(owner,state,std::move(page)),"A real built-in Ticket investigation template creates its independent note");
    const HWND edit=Edit(owner);const auto original=NotesWindowText(edit);CHARRANGE caret{};NotesSelection(edit,caret);
    Check(caret.cpMin==0 && caret.cpMax==0,"The built-in template starts at its actual initial insertion caret without a simulated click");
    CHARFORMAT2W defaults{sizeof(defaults)},typing{sizeof(typing)};
    SendMessageW(edit,EM_GETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&defaults));
    SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&typing));
    const auto existing=NativeRangeInk(edit,0,1);
    std::cout<<"Template initial ink before typing: dark="<<g_bThemeIsDark<<"; default="<<defaults.crTextColor<<"/"<<defaults.dwEffects
        <<"; caret="<<typing.crTextColor<<"/"<<typing.dwEffects<<"; existing="<<existing<<"; expected="<<g_themeColors.crControlText<<std::endl;
    const std::wstring prefix=L"Immediate template typing \x03a9 \x2014 ";
    SendMessageW(edit,EM_STOPGROUPTYPING,0,0);Type(owner,prefix.c_str());SendMessageW(edit,EM_STOPGROUPTYPING,0,0);KillTimer(owner,1);
    const auto inserted=NativeRangeInk(edit,0,static_cast<LONG>(prefix.size()));
    std::cout<<"Template initial ink after typing: inserted="<<inserted<<"; expected="<<g_themeColors.crControlText<<std::endl;
    Check(existing==g_themeColors.crControlText && inserted==g_themeColors.crControlText,
        "Immediate typing at a built-in template's initial caret uses the selected theme foreground, matching its existing prose");
    Check(NotesWindowText(edit)==prefix+original,"The initial Unicode typing preserves the complete existing template text");
    Check(SendMessageW(edit,EM_UNDO,0,0) && NotesWindowText(edit)==original,"One native Undo removes only the initial template prefix");
    Check(SendMessageW(edit,EM_REDO,0,0) && NotesWindowText(edit)==prefix+original,"One native Redo restores the same initial Unicode template prefix");
    Select(edit,0,0);Save(owner);DestroyWindow(owner);
    const HWND reopened=Open(client);const HWND restored=Edit(reopened);const auto savedText=NotesWindowText(restored);
    CHARRANGE restoredCaret{};NotesSelection(restored,restoredCaret);Check(restoredCaret.cpMin==0 && restoredCaret.cpMax==0,"Reopening a saved template restores its initial caret without a click");
    Type(reopened,L"Reopened ");KillTimer(reopened,1);
    Check(NativeRangeInk(restored,0,9)==g_themeColors.crControlText && NotesWindowText(restored)==L"Reopened "+savedText,
        "Immediate prefix typing also uses theme ink after the saved template note is reopened");
    Check(SendMessageW(restored,EM_UNDO,0,0) && NotesWindowText(restored)==savedText,"Reopened template prefix typing remains one native Undo action");
    auto &workspace=State(reopened);NotesTemplates library{reopened,&workspace};Check(NotesLoadTemplates(library),"The editable reusable library loads for its initial typing-ink regression");
    const HWND manager=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_TEMPLATE_EDITOR),reopened,NotesTemplateEditorProc,reinterpret_cast<LPARAM>(&library));
    Check(manager!=nullptr,"A private native template editor is available without showing or activating it");const HWND preview=GetDlgItem(manager,IDC_NOTES_PREVIEW);
    const auto starter=NotesWindowText(preview);CHARRANGE previewCaret{};NotesSelection(preview,previewCaret);SendMessageW(preview,EM_STOPGROUPTYPING,0,0);
    SendMessageW(preview,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"Library "));SendMessageW(preview,EM_STOPGROUPTYPING,0,0);
    Check(NativeRangeInk(preview,previewCaret.cpMin,previewCaret.cpMin+8)==g_themeColors.crControlText,
        "Immediate typing in the editable template library uses the same selected-theme foreground");
    Check(SendMessageW(preview,EM_UNDO,0,0) && NotesWindowText(preview)==starter,"Template-library initial typing retains its native Undo transaction");
    DestroyWindow(manager);ContextTypingInk(reopened);DestroyWindow(reopened);
    NotesBackgroundAudit("initial template typing, reopening, library ink, context contrast and native Undo in both themes");
  }
}
static void LibraryModalReturnFocus() {
  struct RestoreObservers {
    ~RestoreObservers(){notesDialogObserver={};notesPolishFocusObserver={};}
  } observers;
  for(const bool snippets:{true,false}) {
    const auto client=snippets?L"Snippet modal focus QA":L"Template modal focus QA";Fixture(client);
    const HWND owner=Open(client);auto &state=State(owner);Type(owner,L"Before target after");Save(owner);
    const auto original=NotesWindowText(Edit(owner));Select(Edit(owner),7,13);const auto pages=state.pages.size();
    bool returned=false;std::vector<HWND> postReturnFocus;
    notesPolishFocusObserver=[&](HWND target){if(returned)postReturnFocus.push_back(target);};
    notesDialogObserver=[&](LPCWSTR resource,DLGPROC procedure,LPARAM data)->INT_PTR {
      Check(resource==MAKEINTRESOURCEW(IDD_NOTES_TEMPLATE_EDITOR) && procedure==NotesTemplateEditorProc,
          "The library command reaches its real shared modal-return boundary");
      auto &library=*reinterpret_cast<NotesTemplates *>(data);
      Check(library.owner==owner && library.workspace==&state && library.snippets==snippets,
          "The modal library is bound to the intended client and insertion mode");
      client_notes::NotePage page{L"Focus starter",true,NotesPlainRtf(snippets?L"Inserted snippet":L"Template insertion contents"),{}};
      Check(snippets?NotesInsertSnippet(owner,library,page):NotesAddPage(owner,state,std::move(page)),
          "The private modal fixture performs the real successful insertion or template-note transaction");
      returned=true;return IDOK;
    };
    Command(owner,snippets?IDC_NOTES_SNIPPETS:IDC_NOTES_TEMPLATES);
    notesDialogObserver={};notesPolishFocusObserver={};
    Check(returned && postReturnFocus.size()==1 && postReturnFocus.back()==Edit(owner),
        "Successful Insert or Use requests the active document focus after modal teardown, rather than leaving the library button focused");
    if(snippets) {
      const auto inserted=NotesWindowText(Edit(owner));
      Check(state.pages.size()==pages && inserted.find(L"Before ")==0 && inserted.find(L"Inserted snippet")!=std::wstring::npos &&
          inserted.size()>=6 && inserted.substr(inserted.size()-6)==L" after" && inserted.find(L"target")==std::wstring::npos,
          "Returning document focus preserves the snippet's exact replacement target and surrounding client text");
      Check(SendMessageW(Edit(owner),EM_UNDO,0,0) && NotesWindowText(Edit(owner))==original,
          "The snippet remains one native Undo operation after the successful modal-return focus repair");
      Check(SendMessageW(Edit(owner),EM_REDO,0,0) && NotesWindowText(Edit(owner))==inserted,
          "Native Redo restores only that inserted snippet after the focus repair");
    } else Check(state.pages.size()==pages+1 && NotesWindowText(Edit(owner))==L"Template insertion contents",
        "Use template returns focus to its independently created client note");
    KillTimer(owner,1);
    for(const INT_PTR outcome:{static_cast<INT_PTR>(IDCANCEL),static_cast<INT_PTR>(-1)}) {
      Save(owner);const auto before=NotesWindowText(Edit(owner));const auto note=state.attachedPageId;
      const auto durable=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes;
      returned=false;postReturnFocus.clear();
      notesPolishFocusObserver=[&](HWND target){if(returned)postReturnFocus.push_back(target);};
      notesDialogObserver=[&](LPCWSTR,DLGPROC,LPARAM)->INT_PTR{returned=true;return outcome;};
      Command(owner,snippets?IDC_NOTES_SNIPPETS:IDC_NOTES_TEMPLATES);
      notesDialogObserver={};notesPolishFocusObserver={};
      Check(returned && postReturnFocus.empty() && state.attachedPageId==note && NotesWindowText(Edit(owner))==before &&
          client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==durable,
          "Cancel or failed modal creation leaves the invoking focus path, active note and durable content unchanged");
    }
    DestroyWindow(owner);
  }
  Fixture(L"Closed modal owner QA");const HWND closing=Open(L"Closed modal owner QA");Type(closing,L"Keep saved before owner closes");Save(closing);
  bool returned=false;unsigned focusAfterClose=0;
  notesPolishFocusObserver=[&](HWND){if(returned)++focusAfterClose;};
  notesDialogObserver=[&](LPCWSTR,DLGPROC,LPARAM)->INT_PTR{DestroyWindow(closing);returned=true;return IDOK;};
  Command(closing,IDC_NOTES_SNIPPETS);notesDialogObserver={};notesPolishFocusObserver={};
  Check(returned && !IsWindow(closing) && !focusAfterClose,"A stale successful modal result never requests focus through a destroyed Notes owner");
  NotesBackgroundAudit("successful library modal-return focus, native snippet Undo and canceled/stale owners");
}
static void LogicalVisibilityBoundary(HWND owner) {
  const auto rootStyle=NotesNativeWindowLong(owner,GWL_STYLE);
  const HWND child=CreateWindowExW(0,L"STATIC",L"Logical visibility boundary",WS_CHILD,0,0,20,20,owner,nullptr,g_hInst,nullptr);
  Check(child && NotesLogicalVisibilityEligible(child) && !NotesLogicalVisibilityEligible(owner),"Logical visibility is limited to an owned child below a natively hidden root");
  const auto nativeStyle=NotesNativeWindowLong(child,GWL_STYLE);
  ShowWindow(child,SW_SHOW);Check(VisibleRequest(child) && NotesNativeWindowLong(child,GWL_STYLE)==nativeStyle,"A logical child show never modifies its native visibility style");
  ShowWindow(child,SW_HIDE);Check(!VisibleRequest(child) && NotesNativeWindowLong(child,GWL_STYLE)==nativeStyle,"A logical child hide never modifies its native visibility style");
  SetWindowPos(child,nullptr,0,0,20,20,SWP_SHOWWINDOW|SWP_NOACTIVATE|SWP_NOZORDER);
  Check(VisibleRequest(child) && NotesNativeWindowLong(child,GWL_STYLE)==nativeStyle,"A positioned child show preserves native visibility while recording its intended state");
  SetWindowLongPtrW(child,GWL_STYLE,GetWindowLongPtrW(child,GWL_STYLE)|WS_CLIPSIBLINGS);
  Check(VisibleRequest(child) && !(NotesNativeWindowLong(child,GWL_STYLE)&WS_VISIBLE) &&
      (NotesNativeWindowLong(child,GWL_STYLE)&WS_CLIPSIBLINGS),"A native style read/modify/write preserves simulated visibility while applying every ordinary style bit");
  const auto styledNative=NotesNativeWindowLong(child,GWL_STYLE);
  const auto batch=notes_test::DeferPosition(notes_test::BeginPosition(1),child,nullptr,0,0,20,20,SWP_HIDEWINDOW|SWP_NOACTIVATE|SWP_NOZORDER);
  Check(batch && notes_test::EndPosition(batch) && !VisibleRequest(child) && NotesNativeWindowLong(child,GWL_STYLE)==styledNative,"A deferred child hide preserves native visibility and consistent logical state");
  SetWindowLongPtrW(child,GWLP_USERDATA,71);
  Check(GetWindowLongPtrW(child,GWLP_USERDATA)==NotesNativeWindowLong(child,GWLP_USERDATA) &&
      GetWindowLongPtrW(owner,GWL_STYLE)==rootStyle && NotesNativeWindowLong(owner,GWL_STYLE)==rootStyle && !IsWindowVisible(owner),
      "Non-style reads pass through and actual root visibility remains untouched");
  DestroyWindow(child);Check(!notesLogicalChildVisibility.contains(child),"Destroying an owned child removes its logical visibility before native handle reuse");
}
struct MemoryPaint {
  HWND owner=nullptr;HDC original=nullptr,dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ old=nullptr;
  int width=0,height=0;DWORD *pixels=nullptr;
  explicit MemoryPaint(HWND window):owner(window) {
    RECT area{};GetClientRect(window,&area);width=area.right;height=area.bottom;
    if(width<=0 || height<=0 || width>6000 || height>3000) throw std::runtime_error("Polish paint bounds are unavailable or exceed the bounded owned DIB budget");
    original=GetDC(window);dc=CreateCompatibleDC(original);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void *bits=nullptr;bitmap=CreateDIBSection(original,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!original || !dc || !bitmap || !bits) throw std::runtime_error("Owned polish memory paint could not be prepared");
    pixels=static_cast<DWORD *>(bits);old=SelectObject(dc,bitmap);
  }
  ~MemoryPaint(){if(dc&&old)SelectObject(dc,old);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);if(original)ReleaseDC(owner,original);}
  std::vector<DWORD> Snapshot(){GdiFlush();return {pixels,pixels+static_cast<size_t>(width)*height};}
};
struct InkRows {int top=0,bottom=0;unsigned pixels=0;};
static InkRows NativeTextInk(HWND owner,HWND child,bool nativeStatic) {
  MemoryPaint image(child);
  const auto render=[&](const wchar_t *text) {
    SetWindowTextW(child,text);RECT area{0,0,image.width,image.height};FillRect(image.dc,&area,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    if(nativeStatic)SendMessageW(child,WM_PRINT,reinterpret_cast<WPARAM>(image.dc),PRF_CLIENT|PRF_NONCLIENT|PRF_ERASEBKGND);
    else {
      const bool button=GetDlgCtrlID(child)==IDC_NOTES_OPEN_TICKET;
      DRAWITEMSTRUCT draw{static_cast<UINT>(button?ODT_BUTTON:ODT_STATIC),static_cast<UINT>(GetDlgCtrlID(child)),0,ODA_DRAWENTIRE,0,child,image.dc,area,0};
      SendMessageW(owner,WM_DRAWITEM,draw.CtlID,reinterpret_cast<LPARAM>(&draw));
    }
    return image.Snapshot();
  };
  const auto blank=render(L""),text=render(L"Hgpq");InkRows rows{image.height,0,0};
  for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x) {
    const auto offset=static_cast<size_t>(y)*image.width+x;
    if(((blank[offset]^text[offset])&0x00ffffff)!=0){rows.top=(std::min)(rows.top,y);rows.bottom=(std::max)(rows.bottom,y+1);++rows.pixels;}
  }
  Check(rows.pixels>4 && rows.bottom>rows.top,"The actual native tag/ticket text produces measurable glyph ink against its own blank render");return rows;
}
static void TicketTagsBaseline(HWND owner,UINT dpi) {
  const HWND tags=GetDlgItem(owner,IDC_NOTES_CONTEXT);
  const HWND linked=GetDlgItem(owner,IDC_NOTES_OPEN_TICKET);
  const HWND ticket=VisibleRequest(linked)?linked:GetDlgItem(owner,IDC_NOTES_TICKET_REFERENCE);
  if(!VisibleRequest(tags) || !VisibleRequest(ticket))return;
  struct Restore {
    HWND tags,ticket,hot;LONG_PTR style;std::wstring tagText,ticketText;
    ~Restore(){if(IsWindow(tags)){SetWindowLongPtrW(tags,GWL_STYLE,style);SetWindowTextW(tags,tagText.c_str());}if(IsWindow(ticket))SetWindowTextW(ticket,ticketText.c_str());g_hHotButton=hot;}
  } restore{tags,ticket,g_hHotButton,GetWindowLongPtrW(tags,GWL_STYLE),NotesWindowText(tags),NotesWindowText(ticket)};
  g_hHotButton=nullptr;
  const auto tagBounds=Bounds(tags,owner),ticketBounds=Bounds(ticket,owner);
  const auto fits=[&](HWND child,const RECT &bounds) {
    const auto dc=GetDC(child);const auto previous=SelectObject(dc,GetOwnerDrawFont(child));SIZE text{};
    GetTextExtentPoint32W(dc,L"Hgpq",4,&text);SelectObject(dc,previous);ReleaseDC(child,dc);
    return text.cx+ScaleByDpi(child==ticket?16:2,dpi)<=bounds.right-bounds.left;
  };
  // An elided tag group can legitimately be narrower than the probe. Other
  // widths in each theme/DPI must still establish full-glyph coverage below.
  if(!fits(tags,tagBounds) || !fits(ticket,ticketBounds))return;
  const auto ticketInk=NativeTextInk(owner,ticket,false),tagInk=NativeTextInk(owner,tags,true);
  const int ticketTop=static_cast<int>(ticketBounds.top)+ticketInk.top,ticketBottom=static_cast<int>(ticketBounds.top)+ticketInk.bottom;
  const int tagTop=static_cast<int>(tagBounds.top)+tagInk.top,tagBottom=static_cast<int>(tagBounds.top)+tagInk.bottom;
  std::cout<<"Native ticket/tags ink: dpi="<<dpi<<"; ticket="<<ticketTop<<','<<ticketBottom<<"; tags="<<tagTop<<','<<tagBottom<<std::endl;
  Check(std::abs(ticketTop-tagTop)<=1 && std::abs(ticketBottom-tagBottom)<=1,
      "Native tag and ticket glyph baselines align within one physical pixel in the real command row");
  SetWindowLongPtrW(tags,GWL_STYLE,restore.style&~static_cast<LONG_PTR>(SS_CENTERIMAGE));
  const auto oldInk=NativeTextInk(owner,tags,true);
  const int oldTop=static_cast<int>(tagBounds.top)+oldInk.top,oldBottom=static_cast<int>(tagBounds.top)+oldInk.bottom;
  Check(std::abs(ticketTop-oldTop)>1 || std::abs(ticketBottom-oldBottom)>1,
      "The native negative control detects the old top-aligned tag rendering instead of accepting matching rectangles");
  baselineCoverage.insert({g_bThemeIsDark,dpi});
}
static void TabTypography(HWND owner,UINT dpi) {
  auto &state=State(owner);const HWND tabs=GetDlgItem(owner,IDC_NOTES_TABS);
  const HFONT normal=reinterpret_cast<HFONT>(SendMessageW(tabs,WM_GETFONT,0,0)),selected=state.tabFont;
  LOGFONTW regular{},strong{};
  Check(normal && selected && GetObjectW(normal,sizeof(regular),&regular)==sizeof(regular) &&
      GetObjectW(selected,sizeof(strong),&strong)==sizeof(strong),"Both native tab appearances expose real DPI-scaled fonts");
  RECT first{},second{};Check(TabCtrl_GetItemRect(tabs,0,&first) && TabCtrl_GetItemRect(tabs,1,&second),"Typography fixture has separate real tab hit rectangles");
  bool usedNormal=false,usedSelected=false;
  notesPolishFontSelectionObserver=[&](HGDIOBJ object){usedNormal|=object==normal;usedSelected|=object==selected;};
  {
    MemoryPaint image(tabs);SendMessageW(tabs,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(image.dc),PRF_CLIENT);
    Check(!image.Snapshot().empty(),"The actual native tab painter renders into an owned memory bitmap");
  }
  notesPolishFontSelectionObserver={};
  RECT afterFirst{},afterSecond{};TabCtrl_GetItemRect(tabs,0,&afterFirst);TabCtrl_GetItemRect(tabs,1,&afterSecond);
  Check(usedNormal && usedSelected,"Actual tab painting uses both the inactive and selected font paths");
  Check(std::abs(regular.lfHeight)==std::abs(strong.lfHeight) &&
      std::abs(strong.lfHeight)<=MulDiv(11,dpi,72),"Selecting a tab changes emphasis without enlarging its type beyond the compact UI scale");
  Check(EqualRect(&first,&afterFirst) && EqualRect(&second,&afterSecond) &&
      first.right<=second.left && first.bottom==second.bottom,"Normal and selected tab painting preserve separated hit targets and shared row geometry");
}
static void PositionFormatObserved(HWND owner,ClientNotesDialogState &state,UINT dpi) {
  unsigned commands=0,surfaces=0;
  notesPolishPositionObserver=[&](HWND child,HWND after,UINT flags) {
    if(GetParent(child)!=owner)return;
    if(IsNotesFormatControl(GetDlgCtrlID(child))){++commands;Check((flags&SWP_NOZORDER)!=0,"Formatting layout preserves each command's native keyboard traversal order");}
    else if(child==state.formatSurface){++surfaces;Check(after==GetDlgItem(owner,IDC_NOTES_CLEAR) && !(flags&SWP_NOZORDER),"Only the inert palette surface is placed below the format commands and above the editor");}
  };
  PositionNotesFormat(owner,state,dpi);notesPolishPositionObserver={};
  Check(commands>=3 && surfaces==1,"The observed native layout retains real command placements and exactly one palette surface placement");
}
static void FormatReachability(HWND owner,UINT dpi) {
  auto &state=State(owner);const HWND editor=Edit(owner);const auto body=Bounds(editor);
  const std::array controls={IDC_NOTES_STYLE,IDC_NOTES_SIZE,IDC_NOTES_BOLD,IDC_NOTES_ITALIC,
      IDC_NOTES_UNDERLINE,IDC_NOTES_HIGHLIGHT,IDC_NOTES_HIGHLIGHT_COLOR,IDC_NOTES_BULLETS,
      IDC_NOTES_NUMBERS,IDC_NOTES_CHECKLIST,IDC_NOTES_LINK,IDC_NOTES_UNDO,IDC_NOTES_REDO,IDC_NOTES_CLEAR};
  const auto commandOrder=[&] {
    std::vector<int> result;
    for(HWND child=GetWindow(owner,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))
      if(IsNotesFormatControl(GetDlgCtrlID(child)))result.push_back(GetDlgCtrlID(child));
    return result;
  };
  const auto originalOrder=commandOrder();
  const auto inspect=[&](bool expanded) {
    PositionFormatObserved(owner,state,dpi);
    const auto surface=Bounds(state.formatSurface);
    Check(state.formatVisible && Contains(body,surface),"The format popup stays wholly inside the actual editor viewport");
    std::vector<RECT> placed;unsigned reachable=0;
    for(const auto id:controls) {
      const HWND child=GetDlgItem(owner,id);
      if(!VisibleRequest(child)){Check(!expanded,"Secondary formatting is hidden only in the compact presentation");continue;}
      const auto rect=Bounds(child);++reachable;
      Check(Contains(surface,rect),"Every requested formatting control fits completely inside its popup");
      Check(std::none_of(placed.begin(),placed.end(),[&](const RECT &other){RECT overlap{};return IntersectRect(&overlap,&rect,&other)!=FALSE;}),
          "Wrapping the expanded popup leaves distinct usable formatting hit targets");
      placed.push_back(rect);
    }
    const HWND more=GetDlgItem(owner,IDC_NOTES_FORMAT_MORE);const auto moreBounds=Bounds(more);
    Check(VisibleRequest(more) && Contains(surface,moreBounds) && reachable>=3,
        "The compact palette retains useful primary formatting and a reachable More toggle");
    if(expanded) Check(reachable==controls.size(),"Expanding More exposes every existing formatting command without losing a tool");
    Check(commandOrder()==originalOrder,"More and repeated popup positioning retain the native sibling order of every formatting command");
    return reachable;
  };
  Select(editor,2,9);ShowNotesFormat(owner,state,true);
  const auto compact=inspect(false);Command(owner,IDC_NOTES_FORMAT_MORE);const auto expanded=inspect(true);
  Check(expanded>compact,"More expands the palette instead of showing the complete toolbar all the time");
  Command(owner,IDC_NOTES_FORMAT_MORE);Check(inspect(false)==compact,"Collapsing More returns to the same compact primary controls");
  HideNotesFormat(owner,state);
}
static void SearchReachability(HWND owner,UINT dpi) {
  auto &state=State(owner);const HWND editor=Edit(owner);const auto ordinary=Bounds(editor,owner);
  CHARRANGE selection{};NotesSelection(editor,selection);const auto undo=SendMessageW(editor,EM_GETUNDONAME,0,0);
  const auto inspect=[&](bool replace) {
    Command(owner,replace?IDC_NOTES_REPLACE:IDC_NOTES_FIND_TOGGLE);
    SetDlgItemTextW(owner,IDC_NOTES_FIND,L"alpha");SendMessageW(owner,WM_TIMER,kNotesFindTimer,0);
    LayoutClientNotes(owner,dpi);const auto body=Bounds(editor,owner);RECT client{};GetClientRect(owner,&client);
    Check(body.top>ordinary.top && body.top-ordinary.top<=ScaleByDpi(replace?96:64,dpi),
        "Find and Replace reserve compact temporary space instead of a large permanent toolbar");
    std::vector<RECT> occupied;
    for(const auto id:{IDC_NOTES_FIND,IDC_NOTES_FIND_PREV,IDC_NOTES_FIND_NEXT,IDC_NOTES_REPLACE,
        IDC_NOTES_FIND_CLOSE,IDC_NOTES_FIND_MATCH_CASE,IDC_NOTES_FIND_WHOLE_WORD,IDC_NOTES_FIND_WRAP,
        IDC_NOTES_FIND_INFO,IDC_NOTES_REPLACE_TEXT,IDC_NOTES_REPLACE_NEXT,IDC_NOTES_REPLACE_ALL,kNotesFindOptionsId}) {
      const HWND child=GetDlgItem(owner,id);if(!child || !VisibleRequest(child))continue;
      const auto rect=Bounds(child,owner);
      Check(Contains(client,rect) && rect.bottom<=body.top,"Every requested search action and query remains reachable above the editor");
      Check(std::none_of(occupied.begin(),occupied.end(),[&](const RECT &other){RECT overlap{};return IntersectRect(&overlap,&rect,&other)!=FALSE;}),
          "Compact search fields, count, options and actions do not overlap");occupied.push_back(rect);
    }
    SameSelection(editor,selection,"Changing a compact search query and its geometry preserves the document selection");
    Check(SendMessageW(editor,EM_GETUNDONAME,0,0)==undo && !NotesWindowText(GetDlgItem(owner,IDC_NOTES_FIND_INFO)).empty(),
        "Debounced search still reports results without creating a document Undo action");
    Command(owner,IDC_NOTES_FIND_CLOSE);LayoutClientNotes(owner,dpi);
    const auto restored=Bounds(editor,owner);
    Check(EqualRect(&ordinary,&restored),"Closing temporary search restores the exact writing viewport");
  };
  inspect(false);inspect(true);Check(!state.findVisible && !state.replaceVisible,"Find and Replace both dismiss without retaining a second surface");
}
static void QuickRow(HWND owner,UINT dpi,bool requireTools) {
  RECT client{};GetClientRect(owner,&client);std::vector<RECT> occupied;
  for(const int id:{IDC_NOTES_FIND_TOGGLE,IDC_NOTES_SNIPPETS,IDC_NOTES_TEMPLATES,IDC_NOTES_WORKLOG,
      IDC_NOTES_FORMAT,IDC_NOTES_TOOLS,IDC_NOTES_DETAILS,IDC_NOTES_WORK_STATUS,
      IDC_NOTES_OPEN_TICKET,IDC_NOTES_TICKET_REFERENCE,IDC_NOTES_CONTEXT}) {
    const HWND child=GetDlgItem(owner,id);if(!VisibleRequest(child))continue;const auto bounds=Bounds(child,owner);
    Check(Contains(client,bounds),"Every visible quick-row target stays inside the Notes client area");
    Check(std::none_of(occupied.begin(),occupied.end(),[&](const RECT &other){RECT overlap{};return IntersectRect(&overlap,&bounds,&other)!=FALSE;}),
        "Visible quick actions, ticket, tags, status and Details have distinct nonoverlapping targets");occupied.push_back(bounds);
    if(id==IDC_NOTES_CONTEXT)continue; // Tags intentionally elide as a group.
    const auto text=NotesWindowText(child);const HDC dc=GetDC(child);const auto previous=SelectObject(dc,GetOwnerDrawFont(child));SIZE measured{};
    GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&measured);SelectObject(dc,previous);ReleaseDC(child,dc);
    const int padding=notes_editor_ui::HasActionIcon(id)?25:id==IDC_NOTES_WORK_STATUS?26:16;
    Check(measured.cx+ScaleByDpi(padding,dpi)<=bounds.right-bounds.left,
        "Visible action labels and a typical complete ticket number fit beside their icons without clipping");
  }
  const HMENU menu=BuildNotesToolsMenu(owner);Check(menu!=nullptr,"The compact Tools command list can be inspected without showing a popup");
  unsigned hidden=0;
  for(const int id:{IDC_NOTES_FIND_TOGGLE,IDC_NOTES_SNIPPETS,IDC_NOTES_TEMPLATES,IDC_NOTES_WORKLOG,IDC_NOTES_FORMAT}) {
    const bool missing=!VisibleRequest(GetDlgItem(owner,id));const UINT flags=GetMenuState(menu,id,MF_BYCOMMAND);
    if(missing)++hidden;
    Check((flags!=static_cast<UINT>(-1))==missing,"Tools contains exactly the utility actions displaced from the current row");
    if(missing)Check(((flags&MF_GRAYED)!=0)==!IsWindowEnabled(GetDlgItem(owner,id)),"Displaced Tools commands retain their action's enabled state");
  }
  Check(GetMenuItemCount(menu)==static_cast<int>(hidden) && VisibleRequest(GetDlgItem(owner,IDC_NOTES_TOOLS))==(hidden!=0),
      "Tools appears only when needed and preserves every displaced action exactly once");
  if(requireTools)Check(hidden && VisibleRequest(GetDlgItem(owner,IDC_NOTES_TOOLS)),"A narrow 720-DIP window exposes its displaced utilities through labeled Tools");
  DestroyMenu(menu);
  TicketTagsBaseline(owner,dpi);
}
static void QuietActions(HWND owner) {
  auto &state=State(owner);
  for(const auto id:{IDC_NOTES_FIND_TOGGLE,IDC_NOTES_SNIPPETS,IDC_NOTES_TEMPLATES,
      IDC_NOTES_FORMAT,IDC_NOTES_MORE,IDC_NOTES_HELP,IDC_NOTES_DETAILS}) {
    const HWND button=GetDlgItem(owner,id);const auto label=NotesWindowText(button);MemoryPaint image(button);
    const auto paint=[&](UINT appearance,bool hot) {
      g_hHotButton=hot?button:nullptr;SetWindowTextW(button,L"");
      DRAWITEMSTRUCT draw{ODT_BUTTON,static_cast<UINT>(id),0,ODA_DRAWENTIRE,appearance,button,image.dc,{0,0,image.width,image.height},0};
      Check(DrawNotesToolbarButton(draw,state),"The production action painter accepts a quiet control's native states");
      auto pixels=image.Snapshot();SetWindowTextW(button,label.c_str());return pixels;
    };
    PaintNotesChrome(image.dc,button);const auto baseline=image.Snapshot();
    const auto resting=paint(0,false),hot=paint(0,true),disabled=paint(ODS_DISABLED,false),disabledHot=paint(ODS_DISABLED,true);
    const auto sample=static_cast<size_t>(image.height/2)*image.width+(std::min)(5,image.width-1);
    Check(resting[sample]==baseline[sample],"Resting utility actions share the Notes chrome instead of painting a row of filled boxes");
    Check(hot!=resting && disabled==disabledHot,"Quiet actions retain visible hover feedback and an inert disabled appearance");
  }
  g_hHotButton=nullptr;
}
static void Capture(HWND owner,const fs::path &file) {
  auto &state=State(owner);
  if(state.formatVisible)PositionFormatObserved(owner,state,GetDpiForWindow(owner));
  MemoryPaint image(owner);
  // Route the real parent WM_PAINT into the same bounded memory target used
  // by the existing normal-opening fixture. No desktop pixels are sampled.
  notesVirtualPaintWindow=owner;notesVirtualPaintDc=image.dc;
  notesVirtualClipRequested=true;notesVirtualRequestedDirty={0,0,image.width,image.height};
  SendMessageW(owner,WM_PAINT,0,0);
  notesVirtualPaintWindow=nullptr;notesVirtualPaintDc=nullptr;notesVirtualClipRequested=false;
  std::vector<HWND> children;
  for(HWND child=GetWindow(owner,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))
    if(VisibleRequest(child))children.push_back(child);
  if(state.formatVisible) {
    // The background guard suppresses native Z-order changes. After proving
    // the production placement intent above, apply only that inert-surface
    // order to this instrumented memory composition, never to native windows.
    children.erase(std::remove(children.begin(),children.end(),state.formatSurface),children.end());
    const auto clear=std::find(children.begin(),children.end(),GetDlgItem(owner,IDC_NOTES_CLEAR));
    Check(clear!=children.end(),"Expanded palette memory composition retains its real final formatting command");
    children.insert(std::next(clear),state.formatSurface);
  }
  // Low to high native sibling Z order keeps the floating palette above its
  // editor. Each control uses its own native print or production owner painter.
  for(auto i=children.rbegin();i!=children.rend();++i) {
    const HWND child=*i;const auto bounds=Bounds(child,owner);RECT area{};GetClientRect(child,&area);
    if(area.right<=0 || area.bottom<=0)continue;
    const int previous=SaveDC(image.dc);IntersectClipRect(image.dc,bounds.left,bounds.top,bounds.right,bounds.bottom);
    SetViewportOrgEx(image.dc,bounds.left,bounds.top,nullptr);
    wchar_t className[32]{};GetClassNameW(child,className,static_cast<int>(std::size(className)));
    const auto style=GetWindowLongPtrW(child,GWL_STYLE);
    const bool button=!wcscmp(className,L"Button") && (style&BS_TYPEMASK)==BS_OWNERDRAW;
    const bool label=!wcscmp(className,L"Static") && (style&SS_TYPEMASK)==SS_OWNERDRAW;
    if(button || label) {
      DRAWITEMSTRUCT draw{static_cast<UINT>(button?ODT_BUTTON:ODT_STATIC),static_cast<UINT>(GetDlgCtrlID(child)),0,ODA_DRAWENTIRE,
          static_cast<UINT>(IsWindowEnabled(child)?0:ODS_DISABLED),child,image.dc,area,0};
      SendMessageW(owner,WM_DRAWITEM,draw.CtlID,reinterpret_cast<LPARAM>(&draw));
    } else SendMessageW(child,WM_PRINT,reinterpret_cast<WPARAM>(image.dc),PRF_CLIENT|PRF_NONCLIENT|PRF_ERASEBKGND);
    RestoreDC(image.dc,previous);
  }
  GdiFlush();SelectObject(image.dc,image.old);
  const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
  {Gdiplus::Bitmap snapshot(image.bitmap,nullptr);Check(snapshot.Save(file.c_str(),&png,nullptr)==Gdiplus::Ok,"Native polish memory render is saved only to the requested private evidence directory");}
  image.old=SelectObject(image.dc,image.bitmap);
}
static void Run(const fs::path &captureOutput={}) {
  TemplateInitialTypingInk();
  baselineCoverage.clear();
  const auto savedColors=g_themeColors;const auto savedDark=g_bThemeIsDark;
  Fixture(L"Polish client A");Fixture(L"Polish client B");
  HWND other=Open(L"Polish client B");Type(other,L"Only client B keeps this saved text.");Save(other);
  const auto otherRoot=State(other).clientRoot;const auto otherBytes=client_notes::ReadNotebook(otherRoot).notebook_snapshot.bytes;DestroyWindow(other);
  HWND owner=Open(L"Polish client A");auto &state=State(owner);
  LogicalVisibilityBoundary(owner);
  Type(owner,L"First tab remains independent.");Save(owner);Command(owner,IDC_NOTES_ADD_TAB);
  const std::wstring original=L"alpha beta gamma\r\nPrinter rollout handover\r\nKeep native Undo intact.";
  Type(owner,original.c_str());state.pages[state.activePage].name=L"Printer rollout with a useful long title";state.metadataChanged=true;Save(owner);RefreshNotesTabs(owner,state);
  const auto noteId=state.attachedPageId;const HWND editor=Edit(owner);
  SendMessageW(editor,EM_STOPGROUPTYPING,0,0);Select(editor,-1,-1);Type(owner,L" pending local edit");SendMessageW(editor,EM_STOPGROUPTYPING,0,0);KillTimer(owner,1);
  const auto pending=NotesWindowText(editor);Select(editor,2,9);CHARRANGE selected{};NotesSelection(editor,selected);
  const auto undo=SendMessageW(editor,EM_GETUNDONAME,0,0);
  const auto durable=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes;
  unsigned streams=0;
  LoadBakedThemesFromHeader();
  for(const wchar_t *name:{L"Dark - Gothic",L"Marine"}) {
    const auto theme=std::find_if(g_aCustomThemes.begin(),g_aCustomThemes.end(),[&](const ThemeEntry &entry){return entry.name==name;});
    Check(theme!=g_aCustomThemes.end(),"Polish exercises both an existing dark and light theme");
    g_themeColors=theme->colors;g_bThemeIsDark=theme->dark;UpdateThemeBrushes();RefreshNotesForTheme();KillTimer(owner,1);
    std::string themeRtf;Check(StreamClientNotesOut(editor,themeRtf),"Each theme captures its native RTF before layout checks; theme ink is allowed to change");
    SetWindowSubclass(editor,notes_geometry::Streams,498,reinterpret_cast<DWORD_PTR>(&streams));
    for(const UINT dpi:{96u,144u,192u}) for(const int width:{900,1240,900}) {
      std::cout<<"Polish layout matrix: theme="<<(g_bThemeIsDark?"dark":"light")<<"; dpi="<<dpi<<"; width="<<width<<std::endl;
      SetWindowPos(owner,nullptr,0,0,ScaleByDpi(width,dpi),ScaleByDpi(620,dpi),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
      state.findVisible=state.replaceVisible=false;HideNotesFormat(owner,state);LayoutClientNotes(owner,dpi);
      auto &page=state.pages[state.activePage];page.ticket.clear();page.ticketUrl.clear();page.tags.clear();page.status=0;
      UpdateNotesToolbar(owner,state);LayoutClientNotes(owner,dpi);
      const auto stable=Bounds(editor,owner);const auto origin=TextOrigin(owner);RECT document{};
      SendMessageW(editor,EM_GETRECT,0,reinterpret_cast<LPARAM>(&document));
      Check(origin.x>=ScaleByDpi(16,dpi) && origin.x<=ScaleByDpi(40,dpi) &&
          stable.right-stable.left>ScaleByDpi(width-80,dpi),"The native writing area uses modest gutters and retains nearly the full window width");
      for(unsigned variant=0;variant<6;++variant) {
        page.ticket=variant==1 || variant==4 ? L"T20260921.0622" : variant==5 ? std::wstring(128,L'W') : L"";
        page.ticketUrl=variant==4?L"https://tickets.example.test/T20260921.0622":L"";
        page.tags=variant==2 || variant==4 ? std::vector<std::wstring>{L"printing",L"handover"}:std::vector<std::wstring>{};
        if(variant==5)for(unsigned tag=0;tag<16;++tag)page.tags.push_back(L"tag"+std::to_wstring(tag)+std::wstring(26,L'N'));
        page.status=variant==3 || variant==4?2:0;UpdateNotesToolbar(owner,state);LayoutClientNotes(owner,dpi);KillTimer(owner,1);
        const auto now=Bounds(editor,owner);const auto first=TextOrigin(owner);
        Check(EqualRect(&stable,&now) && first.x==origin.x && first.y==origin.y,
            "Empty, ticket, tags, status and maximum-length metadata keep the same editor and text origin");
        StableAccessibleNames(owner);
        if(variant==4)QuickRow(owner,dpi,false);
      }
      Check(Edit(owner)==editor && state.attachedPageId==noteId && NotesWindowText(editor)==pending && !streams,
          "Metadata and DPI layout never replace or stream the native editor or its unsaved text");
      SameSelection(editor,selected,"Theme and repeated narrow/wide layout preserve the exact native selection");
      TabTypography(owner,dpi);FormatReachability(owner,dpi);Select(editor,selected.cpMin,selected.cpMax);
      SearchReachability(owner,dpi);KillTimer(owner,1);
    }
    const UINT nativeDpi=GetDpiForWindow(owner);auto &compactPage=state.pages[state.activePage];
    compactPage.ticket=L"T20260921.0622";compactPage.ticketUrl=L"https://tickets.example.test/T20260921.0622";compactPage.tags={L"printing",L"rollout"};compactPage.status=2;
    UpdateNotesToolbar(owner,state);const auto priorWork=geometryWork;const auto actualWork=NotesWorkArea(owner);
    geometryWork={actualWork.left,actualWork.top,actualWork.left+ScaleByDpi(720,nativeDpi),actualWork.top+ScaleByDpi(720,nativeDpi)};
    SetWindowPos(owner,nullptr,0,0,ScaleByDpi(720,nativeDpi),ScaleByDpi(620,nativeDpi),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    RECT narrowArea{};GetClientRect(owner,&narrowArea);Check(narrowArea.right==ScaleByDpi(720,nativeDpi),"The constrained private work area creates an actual 720-DIP client viewport");
    LayoutClientNotes(owner,nativeDpi);QuickRow(owner,nativeDpi,true);KillTimer(owner,1);QuietActions(owner);geometryWork=priorWork;
    RemoveWindowSubclass(editor,notes_geometry::Streams,498);
    std::string afterTheme;Check(StreamClientNotesOut(editor,afterTheme) && afterTheme==themeRtf,
        "Layout, tab styling, compact search and palette changes preserve the exact native RTF within each theme");
    if(!captureOutput.empty()) {
      fs::create_directories(captureOutput);auto &page=state.pages[state.activePage];
      page.ticket=L"T20260921.0622";page.ticketUrl=L"https://tickets.example.test/T20260921.0622";page.tags={L"printing",L"rollout"};page.status=2;
      UpdateNotesToolbar(owner,state);const UINT dpi=GetDpiForWindow(owner);
      for(const int width:{1100,900}) {
        SetWindowPos(owner,nullptr,0,0,ScaleByDpi(width,dpi),ScaleByDpi(620,dpi),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        LayoutClientNotes(owner,dpi);HideNotesFormat(owner,state);Select(editor,selected.cpMin,selected.cpMax);
        if(width==900){ShowNotesFormat(owner,state,true);Command(owner,IDC_NOTES_FORMAT_MORE);}
        Capture(owner,captureOutput/((g_bThemeIsDark?L"dark-":L"light-")+std::to_wstring(width)+(width==900?L"-format-more.png":L"-writing.png")));
        HideNotesFormat(owner,state);KillTimer(owner,1);
      }
    }
    NotesBackgroundAudit("polish theme, metadata, palette and search matrix");
  }
  Check(!streams && SendMessageW(editor,EM_GETUNDONAME,0,0)==undo && SendMessageW(editor,EM_CANUNDO,0,0),
      "The complete polish matrix preserves the original native Undo action without any rich-text reload");
  Check(NotesWindowText(editor)==pending && client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==durable,
      "The complete presentation review preserves the unsaved text and every durable notebook byte");
  Check(SendMessageW(editor,EM_UNDO,0,0) && NotesWindowText(editor)==original,"One real Undo removes only the independent pending edit after all polish changes");
  Check(SendMessageW(editor,EM_REDO,0,0) && NotesWindowText(editor)==pending,"One real Redo restores that same pending edit after all polish changes");
  Command(owner,IDC_NOTES_DETAILS);const HWND panel=state.panel;Check(panel && IsWindow(panel),"Compact Details opens through its real note action");
  auto *details=reinterpret_cast<NotesPanel *>(GetWindowLongPtrW(panel,DWLP_USER));const auto prior=Bounds(editor);
  RECT area{};GetClientRect(panel,&area);const auto panelBounds=Bounds(panel);const auto panelDpi=GetDpiForWindow(panel);
  Check(panelBounds.bottom-panelBounds.top<=ScaleByDpi(450,panelDpi) && Contains(NotesWorkArea(owner),panelBounds),
      "Unconstrained Details remains compact and inside the actual monitor work area");
  for(const int id:{IDC_NOTES_STATUS_PICK,IDC_NOTES_TAGS_EDIT,IDC_NOTES_INPUT1,IDC_NOTES_INPUT2,IDC_NOTES_LINK_BROWSER_PICK}) {
    const HWND field=GetDlgItem(panel,id);NotesPanelRevealControl(panel,*details,field);const auto rect=Bounds(field,panel);
    Check(rect.top>=details->scrollHeader && rect.bottom<=area.bottom && rect.bottom-rect.top<=ScaleByDpi(36,panelDpi),
        "Every compact Details field remains a complete reachable native edit or closed dropdown");
  }
  const auto unchanged=Bounds(editor);Check(EqualRect(&prior,&unchanged),"Opening and scrolling compact Details never moves its note editor");
  const auto beforeInvalid=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes;
  SetDlgItemTextW(panel,IDC_NOTES_INPUT2,L"not-a-ticket-url");KillTimer(panel,1);
  Check(!NotesApplyDetails(panel,*details) && !state.detailsDrafts.empty() && state.problemVisible,
      "The accessibility regression reaches the real invalid-Details warning state");
  StableAccessibleNames(owner);
  Check(client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==beforeInvalid && NotesWindowText(editor)==pending,
      "Invalid-Details accessibility checks keep the exact saved notebook bytes and unsaved body text");
  SetDlgItemTextW(panel,IDC_NOTES_INPUT1,L"T20260921.0622");SetDlgItemTextW(panel,IDC_NOTES_INPUT2,L"https://tickets.example.test/T20260921.0622");
  SetDlgItemTextW(panel,IDC_NOTES_TAGS_EDIT,L"polish, handover");SendDlgItemMessageW(panel,IDC_NOTES_STATUS_PICK,CB_SETCURSEL,2,0);SendMessageW(panel,WM_TIMER,1,0);
  CloseNotesPanel(state);SendMessageW(owner,WM_TIMER,1,0);
  const auto disk=client_notes::ReadNotebook(state.clientRoot);const auto saved=std::find_if(disk.pages.begin(),disk.pages.end(),[&](const auto &page){return page.id==noteId;});
  Check(saved!=disk.pages.end() && saved->ticket==L"T20260921.0622" && saved->status==2 &&
      saved->tags==std::vector<std::wstring>{L"polish",L"handover"} && NotesPlain(*saved)==pending && !state.saveFailed,
      "Compact Details and quiet autosave persist exact metadata and native text to the correct notebook");
  Check(NotesWindowText(GetDlgItem(owner,IDC_NOTES_STATUS)).empty(),"Successful polished editing still has no permanent Saved or Saving footer");
  Check(SwitchClientNotes(owner,state,L"Polish client B",otherRoot) && g_hNotesWindow==owner &&
      NotesWindowText(Edit(owner))==L"Only client B keeps this saved text.","Polished client switching keeps the same window while loading only the selected client's note");
  StableAccessibleNames(owner);
  Check(client_notes::ReadNotebook(otherRoot).notebook_snapshot.bytes==otherBytes,"The unrelated client's durable notebook stays byte-exact throughout polish and autosave");
  Check(SwitchClientNotes(owner,state,L"Polish client A",g_sDataDir/L"Sites/Polish client A") && NotesWindowText(Edit(owner))==pending,
      "Returning to the source client restores its independently saved note after the visual changes");
  StableAccessibleNames(owner);
  DestroyWindow(owner);g_themeColors=savedColors;g_bThemeIsDark=savedDark;UpdateThemeBrushes();
  notes_geometry::PopupBounds();
  LibraryModalReturnFocus();
  notesPolishFontSelectionObserver={};
  Check(notesLogicalChildVisibility.empty(),"All private Notes child visibility records expire with their native window lifetimes");
  for(const bool dark:{true,false})for(const UINT dpi:{96u,144u,192u})
    Check(baselineCoverage.contains({dark,dpi}),"Native ink alignment and its old-render negative control are covered in both themes at each tested DPI");
  NotesBackgroundAudit("polish final geometry, Undo, autosave and client isolation");
  Check(!backgroundGuard.forbiddenClipboard && !backgroundGuard.forbiddenKeyboard && !backgroundGuard.forbiddenRedraw &&
      !backgroundGuard.nativeVisibleEvents && !backgroundGuard.nativeFocusEvents && !backgroundGuard.nativeForegroundEvents,
      "Polish coverage performs no native show, focus, foreground, clipboard, device-input or foreign-redraw operation");
}
}
