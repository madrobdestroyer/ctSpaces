// Include after the production Notes fixture in NotesFindTests.
// Each image uses the actual native child control's WM_PRINT rendering, placed
// at its live layout rectangle. No window is shown or activated for a capture.
#include <oleacc.h>
#pragma comment(lib,"Oleacc.lib")
static RECT FindControlBounds(HWND owner,int id){RECT bounds{};GetWindowRect(GetDlgItem(owner,id),&bounds);MapWindowPoints(nullptr,owner,reinterpret_cast<POINT *>(&bounds),2);return bounds;}
static void FindCheckEditChromeAndLabels(HWND owner){
 const auto originalInfo=NotesWindowText(GetDlgItem(owner,IDC_NOTES_FIND_INFO));
 for(const auto [id,label]:{std::pair{IDC_NOTES_FIND,L"Find in note"},std::pair{IDC_NOTES_REPLACE_TEXT,L"Replace with"}}){
  const auto control=GetDlgItem(owner,id);Check(control&&(GetWindowLongPtrW(control,GWL_STYLE)&WS_BORDER)==0,"A real native search edit has no classic WS_BORDER frame");
  Check((GetWindowLongPtrW(control,GWL_EXSTYLE)&WS_EX_CLIENTEDGE)==0,"A real native search edit has no raised WS_EX_CLIENTEDGE frame");
  const auto originalValue=NotesWindowText(control);
  for(const auto *notice:{L"Type to search this note",L"11 matches",L"Searching...",L"No matches"}){
   SetDlgItemTextW(owner,IDC_NOTES_FIND_INFO,notice);
   IAccessible *accessible=nullptr;const auto result=AccessibleObjectFromWindow(control,OBJID_CLIENT,IID_IAccessible,reinterpret_cast<void **>(&accessible));
   Check(SUCCEEDED(result)&&accessible,"The search edit retains its native accessible edit object");
   VARIANT self{},role{};self.vt=VT_I4;self.lVal=CHILDID_SELF;BSTR name=nullptr;
   const bool named=SUCCEEDED(accessible->get_accRole(self,&role))&&role.vt==VT_I4&&role.lVal==ROLE_SYSTEM_TEXT&&SUCCEEDED(accessible->get_accName(self,&name))&&name&&std::wstring_view(name,SysStringLen(name))==label;
   SysFreeString(name);VariantClear(&role);accessible->Release();
   Check(named&&NotesWindowText(control)==originalValue,"The native search edit keeps its stable label, edit role and value when the neighboring search notice changes");
  }
 }
 SetDlgItemTextW(owner,IDC_NOTES_FIND_INFO,originalInfo.c_str());
}
static void FindEditChromeRegression(){
 const auto owner=FindFixture(L"Search field chrome and labels",L"printer printer");
 FindQuery(owner,L"printer");Command(owner,IDC_NOTES_REPLACE);SetDlgItemTextW(owner,IDC_NOTES_REPLACE_TEXT,L"device");
 const auto before=NotesWindowText(Edit(owner));FindCheckEditChromeAndLabels(owner);
 Check(NotesWindowText(Edit(owner))==before,"Checking native search-field chrome and accessibility leaves the document unchanged");
 DestroyWindow(owner);NotesBackgroundAudit("search native edit chrome and stable labels");
}
static void FindCaptureSurface(HWND owner,const fs::path &file){
 RECT area{};GetClientRect(owner,&area);const UINT dpi=GetDpiForWindow(owner);const int margin=ScaleByDpi(24,dpi);
 const auto top=FindControlBounds(owner,IDC_NOTES_FIND);const auto bottom=FindControlBounds(owner,IDC_NOTES_REPLACE_TEXT);
 RECT crop{margin,top.top-ScaleByDpi(4,dpi),area.right-margin,bottom.bottom+ScaleByDpi(8,dpi)};
 HDC native=GetDC(owner),image=CreateCompatibleDC(native);const int width=crop.right-crop.left,height=crop.bottom-crop.top;
 BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
 void *pixels=nullptr;HBITMAP bitmap=CreateDIBSection(native,&info,DIB_RGB_COLORS,&pixels,nullptr,0);Check(bitmap&&pixels&&image,"Create an owned in-memory surface for native search controls");const auto prior=SelectObject(image,bitmap);RECT background{0,0,width,height};FillRect(image,&background,g_hbrThemeWindow);
 for(const int id:{IDC_NOTES_FIND,IDC_NOTES_FIND_PREV,IDC_NOTES_FIND_NEXT,IDC_NOTES_REPLACE,IDC_NOTES_FIND_CLOSE,IDC_NOTES_FIND_MATCH_CASE,IDC_NOTES_FIND_WHOLE_WORD,IDC_NOTES_FIND_WRAP,IDC_NOTES_FIND_INFO,IDC_NOTES_REPLACE_TEXT,IDC_NOTES_REPLACE_NEXT,IDC_NOTES_REPLACE_ALL}){
  const auto bounds=FindControlBounds(owner,id);const int saved=SaveDC(image);IntersectClipRect(image,bounds.left-crop.left,bounds.top-crop.top,bounds.right-crop.left,bounds.bottom-crop.top);SetViewportOrgEx(image,bounds.left-crop.left,bounds.top-crop.top,nullptr);
  SendMessageW(GetDlgItem(owner,id),WM_PRINT,reinterpret_cast<WPARAM>(image),PRF_CLIENT|PRF_NONCLIENT|PRF_ERASEBKGND);RestoreDC(image,saved);
 }
 for(const int id:{IDC_NOTES_FIND_MATCH_CASE,IDC_NOTES_FIND_WHOLE_WORD,IDC_NOTES_FIND_WRAP}){
  const auto control=GetDlgItem(owner,id);const auto bounds=FindControlBounds(owner,id);const int size=ScaleByDpi(16,dpi),boxTop=(bounds.bottom-bounds.top-size)/2;
  Check(GetPixel(image,bounds.left-crop.left+ScaleByDpi(8,dpi),bounds.top-crop.top+boxTop+ScaleByDpi(3,dpi))==g_themeColors.crAccent,"The checked native checkbox render uses the active app accent");
  unsigned textPixels=0;for(int y=bounds.top-crop.top;y<bounds.bottom-crop.top;++y)for(int x=bounds.left-crop.left+ScaleByDpi(24,dpi);x<bounds.right-crop.left;++x){const auto pixel=GetPixel(image,x,y);if(abs(static_cast<int>(GetRValue(pixel))-GetRValue(g_themeColors.crWindowText))<32&&abs(static_cast<int>(GetGValue(pixel))-GetGValue(g_themeColors.crWindowText))<32&&abs(static_cast<int>(GetBValue(pixel))-GetBValue(g_themeColors.crWindowText))<32)++textPixels;}
  Check(textPixels>12,"Native checkbox captions contain visible theme-colored text pixels");
  IAccessible *accessible=nullptr;Check(SUCCEEDED(AccessibleObjectFromWindow(control,OBJID_CLIENT,IID_IAccessible,reinterpret_cast<void **>(&accessible)))&&accessible,"The painted option retains its native accessible checkbox object");VARIANT self{},role{},value{};self.vt=VT_I4;self.lVal=CHILDID_SELF;BSTR name=nullptr;const auto label=NotesWindowText(control);const bool accessibleMatches=SUCCEEDED(accessible->get_accRole(self,&role))&&role.vt==VT_I4&&role.lVal==ROLE_SYSTEM_CHECKBUTTON&&SUCCEEDED(accessible->get_accName(self,&name))&&name&&label==name&&SUCCEEDED(accessible->get_accState(self,&value))&&value.vt==VT_I4&&(value.lVal&STATE_SYSTEM_CHECKED);Check(accessibleMatches,"Painting preserves the native checkbox role, label and checked accessibility state");SysFreeString(name);VariantClear(&role);VariantClear(&value);accessible->Release();
 }
 GdiFlush();SelectObject(image,prior);const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};{Gdiplus::Bitmap saved(bitmap,nullptr);Check(saved.Save(file.c_str(),&png,nullptr)==Gdiplus::Ok,"Save the private native search-surface capture");}DeleteObject(bitmap);DeleteDC(image);ReleaseDC(owner,native);
}
static void CaptureFindDetails(HWND owner,const fs::path &file){
 Command(owner,IDC_NOTES_DETAILS);auto &state=State(owner);const auto panel=state.panel;Check(panel&&IsWindow(panel),"The on-demand Details panel opens for native information review");auto *details=reinterpret_cast<NotesPanel *>(GetWindowLongPtrW(panel,DWLP_USER));
 const auto checkHint=[&]{const auto hint=GetDlgItem(panel,IDC_NOTES_DETAILS_HINT);const auto text=NotesWindowText(hint);const auto bounds=FindControlBounds(panel,IDC_NOTES_DETAILS_HINT);RECT area{};GetClientRect(panel,&area);
  Check(text.find(L"regular PC browser")!=std::wstring::npos&&text.find(L"words")!=std::wstring::npos&&text.find(L"characters")!=std::wstring::npos&&text.find(L"lines")!=std::wstring::npos&&text.find(L"selected characters")!=std::wstring::npos,"Details keeps browser guidance and complete document/selection statistics together");
  Check(bounds.left>=0&&bounds.top>=0&&bounds.right<=area.right&&bounds.bottom<=area.bottom,"The full Details information field remains inside the panel client area");
  HDC dc=GetDC(hint);const auto prior=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(hint,WM_GETFONT,0,0)));RECT measured{0,0,bounds.right-bounds.left,0};DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&measured,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX|DT_EXPANDTABS);SelectObject(dc,prior);ReleaseDC(hint,dc);Check(measured.bottom-measured.top<=bounds.bottom-bounds.top,"The real wrapped Details hint text fits its height in the actual native font");
 };
 checkHint();Check(details&&NotesApplyDetails(panel,*details),"Idle Details application completes without a metadata draft error");checkHint();
 RECT area{};GetClientRect(panel,&area);HDC native=GetDC(panel),image=CreateCompatibleDC(native);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=area.right;info.bmiHeader.biHeight=-area.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void *pixels=nullptr;const auto bitmap=CreateDIBSection(native,&info,DIB_RGB_COLORS,&pixels,nullptr,0);Check(bitmap&&pixels&&image,"Create an owned memory surface for native Details controls");const auto prior=SelectObject(image,bitmap);FillRect(image,&area,g_hbrThemeWindow);SendMessageW(panel,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(image),PRF_CLIENT|PRF_ERASEBKGND);
 for(HWND child=GetWindow(panel,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){RECT bounds{};GetWindowRect(child,&bounds);MapWindowPoints(nullptr,panel,reinterpret_cast<POINT *>(&bounds),2);const int saved=SaveDC(image);IntersectClipRect(image,bounds.left,bounds.top,bounds.right,bounds.bottom);SetViewportOrgEx(image,bounds.left,bounds.top,nullptr);SendMessageW(child,WM_PRINT,reinterpret_cast<WPARAM>(image),PRF_CLIENT|PRF_NONCLIENT|PRF_ERASEBKGND);RestoreDC(image,saved);}
 GdiFlush();SelectObject(image,prior);const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};{Gdiplus::Bitmap saved(bitmap,nullptr);Check(saved.Save(file.c_str(),&png,nullptr)==Gdiplus::Ok,"Save native Details content and text-fit review evidence");}DeleteObject(bitmap);DeleteDC(image);ReleaseDC(panel,native);CloseNotesPanel(state);
}
static void CaptureFindVisuals(const fs::path &output){
 const auto owner=FindFixture(L"Search visual review",L"Office printer rollout\r\nalpha alpha alpha\r\nSearch controls are temporary and the note keeps its full editor space when they close.");auto &state=State(owner);fs::create_directories(output);
 for(const bool dark:{true,false})for(const int width:{900,1240}){
  state.findMatchCase=false;state.findWholeWord=false;state.findWrap=true;
  g_bThemeIsDark=dark;BuildThemeColors(dark,g_themeColors);if(dark){LoadBakedThemesFromHeader();const auto theme=std::find_if(g_aCustomThemes.begin(),g_aCustomThemes.end(),[](const auto &entry){return entry.name==L"Dark - Gothic";});Check(theme!=g_aCustomThemes.end(),"Dark visual review uses an actual built-in app theme");g_themeColors=theme->colors;}UpdateThemeBrushes();RefreshNotesForTheme();const auto dpi=GetDpiForWindow(owner);SetWindowPos(owner,nullptr,0,0,ScaleByDpi(width,dpi),ScaleByDpi(620,dpi),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);LayoutClientNotes(owner,dpi);Command(owner,IDC_NOTES_FIND_CLOSE);
  const auto closedBefore=FindControlBounds(owner,IDC_CLIENT_NOTES_TEXT);FindQuery(owner,L"alpha");Command(owner,IDC_NOTES_REPLACE);SetDlgItemTextW(owner,IDC_NOTES_REPLACE_TEXT,L"ready");Command(owner,IDC_NOTES_FIND_MATCH_CASE);Command(owner,IDC_NOTES_FIND_WHOLE_WORD);NotesFindRefresh(owner,state);FindCheckEditChromeAndLabels(owner);
  for(const int id:{IDC_NOTES_FIND,IDC_NOTES_FIND_PREV,IDC_NOTES_FIND_NEXT,IDC_NOTES_REPLACE,IDC_NOTES_FIND_CLOSE,IDC_NOTES_FIND_MATCH_CASE,IDC_NOTES_FIND_WHOLE_WORD,IDC_NOTES_FIND_WRAP,IDC_NOTES_FIND_INFO,IDC_NOTES_REPLACE_TEXT,IDC_NOTES_REPLACE_NEXT,IDC_NOTES_REPLACE_ALL}){const auto bounds=FindControlBounds(owner,id);RECT area{};GetClientRect(owner,&area);Check(bounds.left>=0&&bounds.top>=0&&bounds.right<=area.right&&bounds.bottom<=area.bottom,"A native search control fits inside the private Notes client area");}
  for(const int id:{IDC_NOTES_FIND_MATCH_CASE,IDC_NOTES_FIND_WHOLE_WORD,IDC_NOTES_FIND_WRAP}){const auto control=GetDlgItem(owner,id);const auto bounds=FindControlBounds(owner,id);HDC dc=GetDC(control);const auto prior=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0)));const auto label=NotesWindowText(control);SIZE text{};GetTextExtentPoint32W(dc,label.c_str(),static_cast<int>(label.size()),&text);SelectObject(dc,prior);ReleaseDC(control,dc);Check(text.cx+ScaleByDpi(24,dpi)<=bounds.right-bounds.left,"A native checkbox has room for its glyph and complete label");}
  Select(Edit(owner),LONG_MAX,LONG_MAX);Command(owner,IDC_NOTES_FIND_NEXT);
  {const auto control=GetDlgItem(owner,IDC_NOTES_FIND_INFO);const auto bounds=FindControlBounds(owner,IDC_NOTES_FIND_INFO);HDC dc=GetDC(control);const auto prior=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0)));const auto label=NotesWindowText(control);SIZE text{};GetTextExtentPoint32W(dc,label.c_str(),static_cast<int>(label.size()),&text);SelectObject(dc,prior);ReleaseDC(control,dc);Check(label.find(L"Wrapped to start")!=std::wstring::npos&&text.cx<=bounds.right-bounds.left,"The native wrap notice and current-match count fit without clipping");}
  FindCaptureSurface(owner,output/((dark?L"dark-":L"light-")+std::to_wstring(width)+L"-replace.png"));Command(owner,IDC_NOTES_FIND_CLOSE);const auto closedAfter=FindControlBounds(owner,IDC_CLIENT_NOTES_TEXT);Check(EqualRect(&closedBefore,&closedAfter),"Closing temporary Find and Replace restores exactly the original editor rectangle");if(width==900)CaptureFindDetails(owner,output/(dark?L"dark-details.png":L"light-details.png"));NotesBackgroundAudit("private native search visual capture");
 }
 DestroyWindow(owner);
}
