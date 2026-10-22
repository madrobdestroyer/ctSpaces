// Production spelling UI under the existing strictly hidden Notes guards.
#include <oleacc.h>
#pragma comment(lib,"Oleacc.lib")
#define wmain HistoricalNotesWorkspaceEntryNotRun
#include "NotesWorkspaceTests.cpp"
#undef wmain
#include "RetainedFixture.h"
namespace spelling_helpers {
#include "NotesSpellingHelperTests.inl"
}
static unsigned spellChecks=0,spellFailures=0;
static void SpellCheck(bool value,const char *label){++spellChecks;if(!value)++spellFailures;std::cout<<(value?"PASS ":"FAIL ")<<label<<'\n';}
static bool spellingRejectSelection=false,spellingRejectReplacement=false;
static LRESULT CALLBACK SpellingFailureSubclass(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
  if((message==EM_EXSETSEL&&spellingRejectSelection)||(message==EM_REPLACESEL&&spellingRejectReplacement))return 0;
  return DefSubclassProc(window,message,w,l);
}
static void ReadAndReplaceCases(HWND owner){
  auto&state=State(owner);auto edit=Edit(owner);Select(edit,0,-1);Type(owner,L"hello speling \U0001f680\rordinary ending");Save(owner);
  notes_spelling::ComPtr<ITextDocument> document;notes_spelling::TextDocument(edit,document);notes_spelling::ComPtr<ITextRange> range;document->Range(0,0,&range);LONG length=0;range->GetStoryLength(&length);
  notes_spelling::Range scope{0,length-1};std::wstring original,mask;SpellCheck(SUCCEEDED(notes_spelling::ReadScope(edit,scope,original,mask)),"Production note spelling snapshot reads native UTF-16 positions");
  const auto pos=original.find(L"speling");SpellCheck(pos!=original.npos,"Misspelled fixture word has an exact native range");
  SendMessageW(edit,EM_STOPGROUPTYPING,0,0);SendMessageW(edit,EM_EMPTYUNDOBUFFER,0,0);
  CHARRANGE selected{0,0};SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
  bool changed=false;std::wstring updated,updatedMask;
  const bool loading=state.loading;state.loading=true;
  const auto result=notes_spelling::ReplaceWord(edit,scope,original,{pos,7,{}},L"spelling",changed,updated,updatedMask);state.loading=loading;
  SpellCheck(SUCCEEDED(result)&&changed&&updated==L"hello spelling \U0001f680\rordinary ending","Explicit correction changes only its exact word and preserves emoji and neighboring text");
  SpellCheck(SendMessageW(edit,EM_CANUNDO,0,0)!=0,"One explicit spelling correction creates an Undo entry");
  SendMessageW(edit,EM_UNDO,0,0);std::wstring restored;notes_spelling::NativeText(edit,scope,restored);SpellCheck(restored==original,"One native Undo restores the entire original misspelled note");
  SpellCheck(SendMessageW(edit,EM_CANUNDO,0,0)==0,"One explicit correction consumes exactly one Undo action");
  std::string before;StreamClientNotesOut(edit,before);
  changed=false;SpellCheck(FAILED(notes_spelling::ReplaceWord(edit,scope,original+L"stale",{pos,7,{}},L"spelling",changed,updated,updatedMask))&&!changed,"A stale text snapshot rejects without changing the document");
  changed=false;SpellCheck(FAILED(notes_spelling::ReplaceWord(edit,scope,original,{pos,7,{}},L"$command",changed,updated,updatedMask))&&!changed,"Technical command text cannot become a spelling replacement");
  std::string after;StreamClientNotesOut(edit,after);SpellCheck(before==after,"Rejected spelling changes preserve exact native RTF");
  // Undo can leave the original word already selected. Put the caret elsewhere
  // before rejecting EM_EXSETSEL so this fixture actually denies the target.
  Select(edit,0,0);
  if(!SetWindowSubclass(edit,SpellingFailureSubclass,0x535046,0))throw std::runtime_error("Native spelling failure subclass unavailable");
  spellingRejectSelection=true;changed=false;
  SpellCheck(FAILED(notes_spelling::ReplaceWord(edit,scope,original,{pos,7,{}},L"spelling",changed,updated,updatedMask))&&!changed,"A rejected native target selection prevents the spelling replacement");
  spellingRejectSelection=false;StreamClientNotesOut(edit,after);SpellCheck(before==after,"A native selection failure preserves exact RTF");
  spellingRejectReplacement=true;changed=false;
  SpellCheck(FAILED(notes_spelling::ReplaceWord(edit,scope,original,{pos,7,{}},L"spelling",changed,updated,updatedMask))&&!changed,"A rejected native word replacement is detected by exact text readback");
  spellingRejectReplacement=false;RemoveWindowSubclass(edit,SpellingFailureSubclass,0x535046);StreamClientNotesOut(edit,after);SpellCheck(before==after,"A native replacement failure preserves exact RTF without rewriting the document");
  state.formattingChanged=true;Save(owner);
}
static NotesSpellingState MakeSpellingState(HWND owner){
  NotesSpellingState check;const auto&state=State(owner);check.owner=owner;check.edit=Edit(owner);check.client=state.clientName;check.root=state.clientRoot;check.page=state.attachedPageId;
  notes_spelling::ComPtr<ITextDocument> document;notes_spelling::TextDocument(check.edit,document);notes_spelling::ComPtr<ITextRange> range;document->Range(0,0,&range);LONG length=0;range->GetStoryLength(&length);check.scope={0,length-1};
  Check(SUCCEEDED(notes_spelling::ReadScope(check.edit,check.scope,check.snapshot,check.masked)),"Prepare private spelling UI snapshot");
  Check(SUCCEEDED(check.dictionary.Initialize())&&SUCCEEDED(check.dictionary.Languages(check.languages))&&!check.languages.empty(),"Prepare installed Windows spelling dictionaries");return check;
}
static void DialogCases(HWND owner){
  auto&state=State(owner);auto edit=Edit(owner);Select(edit,0,-1);Type(owner,L"The spelingggg needs review.\rKeep T20260921.0622 https://example.test unchanged.");Save(owner);
  const auto before=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes;std::string rtf;StreamClientNotesOut(edit,rtf);Select(edit,3,3);POINT scroll{};SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
  auto check=MakeSpellingState(owner);const auto dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_SPELLING),owner,NotesSpellingProc,reinterpret_cast<LPARAM>(&check));Check(dialog&&IsWindow(dialog),"Native hidden spelling dialog opens");
  SpellCheck(SendDlgItemMessageW(dialog,IDC_NOTES_SPELL_LANGUAGE,CB_GETCOUNT,0,0)==static_cast<LRESULT>(check.languages.size()),"Spelling dialog exposes exactly the installed Windows language choices");
  SpellCheck(!IsWindowEnabled(GetDlgItem(dialog,IDC_NOTES_SPELL_CHANGE)),"Change is disabled while checking and never applies automatically");
  for(unsigned step=0;!check.error&&check.scan<check.masked.size()&&step<256;++step)NotesSpellingStep(dialog,check);
  SpellCheck(check.error.has_value()&&!NotesWindowText(GetDlgItem(dialog,IDC_NOTES_SPELL_WORD)).empty(),"Bounded spelling timer steps reveal a real misspelled prose word");
  SendMessageW(dialog,WM_COMMAND,IDC_NOTES_SPELL_IGNORE_ALL,0);SpellCheck(!check.ignored.empty(),"Ignore word is confined to this spelling-dialog session");
  std::string after;StreamClientNotesOut(edit,after);CHARRANGE actual{};NotesSelection(edit,actual);POINT actualScroll{};SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&actualScroll));
  SpellCheck(rtf==after&&actual.cpMin==3&&actual.cpMax==3&&actualScroll.x==scroll.x&&actualScroll.y==scroll.y,"Checking and ignoring preserve exact RTF, selection and scroll");
  SpellCheck(client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==before,"Read-only spelling review never rewrites the saved notebook");
  // Presenting another note under the same window must invalidate the old
  // dialog even when that notebook-local note ID could exist elsewhere.
  const auto originalPage=state.attachedPageId;state.attachedPageId=originalPage+100;
  SpellCheck(!NotesSpellingWorkspace(check)&&!NotesSpellingUnchanged(check),"A changed active-note identity invalidates a pending spelling dialog");state.attachedPageId=originalPage;
  const auto originalClient=state.clientName;state.clientName=L"Different client";
  SpellCheck(!NotesSpellingWorkspace(check),"A client switch cannot reuse a previous client's pending correction");state.clientName=originalClient;
  const auto originalRoot=state.clientRoot;state.clientRoot=originalRoot/L"wrong";
  SpellCheck(!NotesSpellingWorkspace(check),"A changed client directory invalidates pending spelling actions");state.clientRoot=originalRoot;
  DestroyWindow(dialog);
  const auto observer=notesDialogObserver;
  notesDialogObserver=[&](LPCWSTR resource,DLGPROC procedure,LPARAM data)->INT_PTR{
    if(IS_INTRESOURCE(resource)&&reinterpret_cast<ULONG_PTR>(resource)==IDD_NOTES_SPELLING){
      auto&captured=*reinterpret_cast<NotesSpellingState*>(data);SpellCheck(captured.owner==owner&&captured.edit==edit&&captured.client==state.clientName&&captured.page==state.attachedPageId,"Searchable spelling action captures exact client, page and native editor identity");
      SpellCheck(procedure==NotesSpellingProc,"Check spelling action opens the intended themed spelling dialog");return IDCANCEL;
    }return IDCANCEL;
  };
  Command(owner,IDC_NOTES_SPELLING);notesDialogObserver=observer;
  NotesSelection(edit,actual);SpellCheck(actual.cpMin==3&&actual.cpMax==3,"Canceling spelling action preserves the original native selection");
}
#include "NotesSpellingLimitsTests.inl"
#include "NotesSpellingPartialTests.inl"
static RECT SpellBounds(HWND dialog,HWND control){RECT result{};GetWindowRect(control,&result);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT*>(&result),2);return result;}
static bool SpellAccessible(HWND control,LONG expectedRole){
  notes_spelling::ComPtr<IAccessible> accessible;if(FAILED(AccessibleObjectFromWindow(control,OBJID_CLIENT,__uuidof(IAccessible),reinterpret_cast<void**>(accessible.GetAddressOf()))))return false;
  VARIANT self{};self.vt=VT_I4;self.lVal=CHILDID_SELF;VARIANT role{};BSTR name=nullptr;
  const bool result=SUCCEEDED(accessible->get_accRole(self,&role))&&role.vt==VT_I4&&role.lVal==expectedRole&&SUCCEEDED(accessible->get_accName(self,&name))&&name&&SysStringLen(name)>0;
  SysFreeString(name);VariantClear(&role);return result;
}
static void SpellDropdownRows(HWND dialog,const fs::path&captures){
  const UINT dpi=GetDpiForWindow(dialog);const int width=ScaleByDpi(360,dpi),height=ScaleByDpi(32,dpi);
  HDC reference=GetDC(dialog),dc=CreateCompatibleDC(reference);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
  DWORD*pixels=nullptr;const auto bitmap=CreateDIBSection(reference,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);if(!dc||!bitmap||!pixels)throw std::runtime_error("Spelling dropdown-row DIB unavailable");const auto old=SelectObject(dc,bitmap);
  const std::wstring longest(notes_spelling::kMaxSuggestion,L'W');
  const auto boundary=SendDlgItemMessageW(dialog,IDC_NOTES_SPELL_SUGGESTIONS,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(longest.c_str()));
  const std::array rows={std::pair{IDC_NOTES_SPELL_LANGUAGE,LRESULT{0}},std::pair{IDC_NOTES_SPELL_SUGGESTIONS,LRESULT{0}},std::pair{IDC_NOTES_SPELL_SUGGESTIONS,boundary}};
  for(size_t entry=0;entry<rows.size();++entry){
    const auto[id,index]=rows[entry];const auto combo=GetDlgItem(dialog,id);Check(index>=0&&SendMessageW(combo,CB_GETCOUNT,0,0)>index,"Native spelling dropdown fixture contains its real or boundary item");bool both=true;
    for(const bool selected:{false,true}){
      std::fill(pixels,pixels+static_cast<size_t>(width)*height,0x00377A21);DRAWITEMSTRUCT draw{};draw.CtlType=ODT_COMBOBOX;draw.CtlID=id;draw.itemID=static_cast<UINT>(index);draw.itemAction=ODA_DRAWENTIRE;draw.itemState=selected?ODS_SELECTED:0;draw.hwndItem=combo;draw.hDC=dc;draw.rcItem={0,0,width,height};
      SendMessageW(dialog,WM_DRAWITEM,id,reinterpret_cast<LPARAM>(&draw));GdiFlush();const auto color=selected?g_themeColors.crAccent:g_themeColors.crControl;const DWORD background=(GetRValue(color)<<16)|(GetGValue(color)<<8)|GetBValue(color);
      size_t ink=0;for(int y=1;y<height-1;++y)for(int x=ScaleByDpi(12,dpi);x<width-1;++x)if((pixels[static_cast<size_t>(y)*width+x]&0xffffff)!=background)++ink;
      both=both&&(pixels[0]&0xffffff)==background&&ink>5;
      const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};Gdiplus::Bitmap image(bitmap,nullptr);const auto file=std::wstring(g_bThemeIsDark?L"dark":L"light")+L"-dropdown-"+std::to_wstring(entry)+(selected?L"-selected.png":L"-normal.png");if(image.Save((captures/file).c_str(),&png,nullptr)!=Gdiplus::Ok)throw std::runtime_error("Spelling dropdown-row capture failed");
    }
    SpellCheck(both,entry==2?"A 128-character spelling suggestion renders visible dropdown text in normal and selected states":"Both spelling dropdowns route real normal and selected native rows to visible themed text");
  }
  SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(dialog,reference);
}
static void SpellCapture(HWND dialog,const fs::path &path){
  RECT area{};GetClientRect(dialog,&area);HDC reference=GetDC(dialog),dc=CreateCompatibleDC(reference);
  BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=area.right;info.bmiHeader.biHeight=-area.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
  DWORD *pixels=nullptr;HBITMAP bitmap=CreateDIBSection(reference,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);
  if(!reference||!dc||!bitmap||!pixels)throw std::runtime_error("Spelling memory capture allocation failed");
  const auto previous=SelectObject(dc,bitmap);std::fill(pixels,pixels+static_cast<size_t>(area.right)*area.bottom,0x00377A21);
  // WM_PRINT targets this process-owned DIB. No desktop capture, presentation,
  // foreground activation or device-input message is involved.
  FillRect(dc,&area,g_hbrThemeWindow);
  const int parentSaved=SaveDC(dc);SendMessageW(dialog,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_ERASEBKGND);RestoreDC(dc,parentSaved);
  // Parent WM_PRINT CHILDREN applies a nonclient offset to this client-only
  // surface. Print each actual child once at its mapped native window bounds.
  for(HWND child=GetWindow(dialog,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
    const auto rect=SpellBounds(dialog,child);const int saved=SaveDC(dc);
    IntersectClipRect(dc,rect.left,rect.top,rect.right,rect.bottom);SetViewportOrgEx(dc,rect.left,rect.top,nullptr);
    SendMessageW(child,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_NONCLIENT|PRF_ERASEBKGND);RestoreDC(dc,saved);
  }
  GdiFlush();const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
  const auto color=pixels[static_cast<size_t>(ScaleByDpi(3,GetDpiForWindow(dialog)))*area.right+ScaleByDpi(3,GetDpiForWindow(dialog))]&0xffffff;
  const auto expected=(GetRValue(g_themeColors.crWindow)<<16)|(GetGValue(g_themeColors.crWindow)<<8)|GetBValue(g_themeColors.crWindow);
  SpellCheck(color==expected,"Spelling native memory render uses the active application background theme");
  bool buttonEdges=true;
  for(const int id:{IDC_NOTES_SPELL_CHANGE,IDC_NOTES_SPELL_IGNORE,IDC_NOTES_SPELL_IGNORE_ALL,IDCANCEL}){
    const auto rect=SpellBounds(dialog,GetDlgItem(dialog,id));const int cx=(rect.left+rect.right)/2,cy=(rect.top+rect.bottom)/2;
    const auto ink=[&](int left,int top,int right,int bottom){for(int y=top;y<bottom;++y)for(int x=left;x<right;++x)if(x>=0&&y>=0&&x<area.right&&y<area.bottom){const auto pixel=pixels[static_cast<size_t>(y)*area.right+x]&0xffffff;if(pixel!=expected&&pixel!=0x00377A21)return true;}return false;};
    buttonEdges=buttonEdges&&ink(rect.left,cy-2,rect.left+3,cy+3)&&ink(rect.right-3,cy-2,rect.right,cy+3)&&ink(cx-2,rect.top,cx+3,rect.top+3)&&ink(cx-2,rect.bottom-3,cx+3,rect.bottom);
  }
  SpellCheck(buttonEdges,"Native spelling button surfaces render at all four edges without clipping");
  SelectObject(dc,previous);{Gdiplus::Bitmap image(bitmap,nullptr);SpellCheck(image.Save(path.c_str(),&png,nullptr)==Gdiplus::Ok,"Save real native spelling client-surface render without showing a window");}
  DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(dialog,reference);
}
static void VisualCases(HWND owner,const fs::path &captures){
  fs::create_directories(captures);Select(Edit(owner),0,-1);Type(owner,L"Please check the speling in this note.\rReview is manual and code is skipped.");Save(owner);
  const auto originalColors=g_themeColors;const bool originalDark=g_bThemeIsDark;LoadBakedThemesFromHeader();
  for(const auto *themeName:{L"Dark - Gothic",L"Marine"}){
    const auto theme=std::find_if(g_aCustomThemes.begin(),g_aCustomThemes.end(),[&](const auto&entry){return entry.name==themeName;});Check(theme!=g_aCustomThemes.end(),"Use an actual bundled spelling dialog theme");g_themeColors=theme->colors;g_bThemeIsDark=theme->dark;UpdateThemeBrushes();
    auto check=MakeSpellingState(owner);HWND dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_SPELLING),owner,NotesSpellingProc,reinterpret_cast<LPARAM>(&check));Check(dialog&&IsWindow(dialog),"Native themed spelling dialog creates hidden controls");
    for(unsigned step=0;!check.error&&check.scan<check.masked.size()&&step<256;++step)NotesSpellingStep(dialog,check);KillTimer(dialog,kNotesSpellingTimer);
    RECT area{};GetClientRect(dialog,&area);const UINT dpi=GetDpiForWindow(dialog);
    SpellCheck(area.right>0&&area.bottom>0&&area.right<=ScaleByDpi(800,dpi)&&area.bottom<=ScaleByDpi(500,dpi),"Spelling remains a compact dialog at the host's actual DPI");
    bool contained=true,nonoverlap=true,textFits=true;std::vector<RECT> bounds;
    for(HWND child=GetWindow(dialog,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
      const auto rect=SpellBounds(dialog,child);contained=contained&&rect.left>=0&&rect.top>=0&&rect.right<=area.right&&rect.bottom<=area.bottom&&rect.right>rect.left&&rect.bottom>rect.top;
      for(const auto &prior:bounds){RECT intersection{};if(IntersectRect(&intersection,&rect,&prior))nonoverlap=false;}bounds.push_back(rect);
      wchar_t kind[32]{};GetClassNameW(child,kind,32);const bool button=!_wcsicmp(kind,L"BUTTON"),label=!_wcsicmp(kind,L"STATIC");
      if((button||label)&&!NotesWindowText(child).empty()){
        HDC dc=GetDC(child);const auto font=reinterpret_cast<HFONT>(SendMessageW(child,WM_GETFONT,0,0));const auto previous=SelectObject(dc,font?font:GetStockObject(DEFAULT_GUI_FONT));
        RECT measured{0,0,rect.right-rect.left-(button?ScaleByDpi(12,dpi):0),0};const auto text=NotesWindowText(child);DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&measured,DT_CALCRECT|DT_NOPREFIX|(button?DT_SINGLELINE:DT_WORDBREAK));
        textFits=textFits&&measured.right<=rect.right-rect.left&&measured.bottom<=rect.bottom-rect.top;SelectObject(dc,previous);ReleaseDC(child,dc);
      }
    }
    SpellCheck(contained&&nonoverlap,"Every spelling label, field and button remains within the client area without overlap");
    SpellCheck(textFits,"Native labels, hints and button text fit their actual themed font bounds");
    const auto word=GetDlgItem(dialog,IDC_NOTES_SPELL_WORD);
    SpellCheck(word&&(GetWindowLongPtrW(word,GWL_STYLE)&WS_BORDER)==0,"The real native spelling word field has no classic WS_BORDER frame");
    SpellCheck((GetWindowLongPtrW(word,GWL_EXSTYLE)&WS_EX_CLIENTEDGE)==0,"The real native spelling word field has no raised WS_EX_CLIENTEDGE frame");
    SpellCheck((GetWindowLongPtrW(word,GWL_STYLE)&ES_READONLY)!=0,"The themed spelling word field retains its native read-only behavior");
    bool accessible=true,tabStops=true;
    for(const int id:{IDC_NOTES_SPELL_CHANGE,IDC_NOTES_SPELL_IGNORE,IDC_NOTES_SPELL_IGNORE_ALL,IDCANCEL}){const auto control=GetDlgItem(dialog,id);accessible=accessible&&SpellAccessible(control,ROLE_SYSTEM_PUSHBUTTON);tabStops=tabStops&&(GetWindowLongPtrW(control,GWL_STYLE)&WS_TABSTOP)!=0;}
    for(const int id:{IDC_NOTES_SPELL_LANGUAGE,IDC_NOTES_SPELL_SUGGESTIONS}){const auto control=GetDlgItem(dialog,id);accessible=accessible&&SpellAccessible(control,ROLE_SYSTEM_COMBOBOX);tabStops=tabStops&&(GetWindowLongPtrW(control,GWL_STYLE)&WS_TABSTOP)!=0;}
    accessible=accessible&&SpellAccessible(GetDlgItem(dialog,IDC_NOTES_SPELL_WORD),ROLE_SYSTEM_TEXT);
    SpellCheck(accessible,"Native MSAA exposes nonempty accessible names and correct roles for every spelling field and action");
    SpellCheck(tabStops,"Language, suggestions and explicit spelling actions expose keyboard tab stops");
    SpellDropdownRows(dialog,captures);SpellCapture(dialog,captures/(g_bThemeIsDark?L"spelling-dark.png":L"spelling-light.png"));DestroyWindow(dialog);
  }
  g_themeColors=originalColors;g_bThemeIsDark=originalDark;UpdateThemeBrushes();
}
int wmain(int argc,wchar_t**argv){try{
  std::cout<<std::unitbuf;backgroundNotesTests=true;NotesBackgroundEvents events;events.Start();CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
  Gdiplus::GdiplusStartupInput graphics;Gdiplus::GdiplusStartup(&g_gdiplusToken,&graphics,nullptr);g_hInst=GetModuleHandleW(nullptr);g_bThemeIsDark=true;BuildThemeColors(true,g_themeColors);UpdateThemeBrushes();
  g_sDataDir=retained_fixture::CreateRoot(fs::current_path()/L"build",L"spelling-private");Check(fs::is_directory(g_sDataDir),"Fresh private spelling fixture");g_sConfigPath=g_sDataDir/L"config.ini";
  g_hGui=notes_test::CreateNoteWindow(0,L"STATIC",L"Private spelling parent",WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,g_hInst,nullptr);
  const bool nativeLimit=argc==2&&!wcscmp(argv[1],L"--native-limit-background");
  const bool partialCorrection=argc==2&&!wcscmp(argv[1],L"--partial-correction-background");
  const bool friendlyLinkSuccess=argc==2&&!wcscmp(argv[1],L"--friendly-link-success-background");
  if(!nativeLimit&&!partialCorrection&&!friendlyLinkSuccess){spelling_helpers::FilterCases();spelling_helpers::NativeReadOnlyCases();spelling_helpers::WindowsProviderCases();}
  Fixture(L"Spelling Alpha");const auto owner=Open(L"Spelling Alpha");
  if(partialCorrection)SpellingPartialCorrectionCases();
  else if(friendlyLinkSuccess)SpellingFriendlyLinkSuccessCases();
  else if(nativeLimit){SpellingNativeLimitCases(owner);SpellingSoftLineCases(owner);}
  else{ReadAndReplaceCases(owner);DialogCases(owner);SpellingNativeLimitCases(owner);SpellingSoftLineCases(owner);SpellingPartialCorrectionCases();SpellingFriendlyLinkSuccessCases();VisualCases(owner,argc>1?fs::absolute(argv[1]):g_sDataDir/L"captures");}
  DestroyWindow(owner);
  DestroyWindow(g_hGui);g_hGui=nullptr;NotesBackgroundAudit("spelling cleanup");NotesBackgroundBanner();
  const auto failures=spellFailures+spelling_helpers::failures;std::cout<<"Notes spelling: "<<spellChecks<<" UI checks, "<<spelling_helpers::checks<<" helper checks, "<<failures<<" failures; actual input/foreground/clipboard=0.\n";
  Gdiplus::GdiplusShutdown(g_gdiplusToken);CoUninitialize();return failures?1:0;
}catch(const std::exception&error){std::cerr<<"Spelling fixture failure: "<<error.what()<<'\n';NotesBackgroundBanner();return 2;}}
