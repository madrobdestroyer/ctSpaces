// Printing uses real RichEdit pagination into a private memory DC. Printer
// dialogs and spooler calls are intercepted before any external operation.
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shldisp.h>
#include <shobjidl.h>
static unsigned printStarts=0,printPages=0,printEnds=0,printAborts=0;
static bool failPrintPage=false;
static int WINAPI TestPrintStart(HDC,const DOCINFOW *){++printStarts;return 1;}
static int WINAPI TestPrintPage(HDC){++printPages;return failPrintPage?-1:1;}
static int WINAPI TestPrintPageEnd(HDC){return 1;}
static int WINAPI TestPrintEnd(HDC){++printEnds;return 1;}
static int WINAPI TestPrintAbort(HDC){++printAborts;return 1;}
#define StartDocW TestPrintStart
#define StartPage TestPrintPage
#define EndPage TestPrintPageEnd
#define EndDoc TestPrintEnd
#define AbortDoc TestPrintAbort
#define wmain HistoricalEditingExtrasEntryNotRun
#include "NotesWorkspaceTests.cpp"
#undef wmain
#undef StartDocW
#undef StartPage
#undef EndPage
#undef EndDoc
#undef AbortDoc
static unsigned extraScenarios=0;
static HWND ExtrasFixture(const wchar_t *name,const wchar_t *text){++extraScenarios;Fixture(name);const auto owner=Open(name);Command(owner,IDC_NOTES_ADD_TAB);Type(owner,text);Save(owner);return owner;}
static std::string ExtrasRtf(HWND edit){std::string rtf;Check(StreamClientNotesOut(edit,rtf),"The native editor exports its exact rich-text baseline");return rtf;}
static CHARRANGE ExtrasSelection(HWND edit){CHARRANGE range{};NotesSelection(edit,range);return range;}
static bool SameSelection(CHARRANGE a,CHARRANGE b){return a.cpMin==b.cpMin&&a.cpMax==b.cpMax;}
static void TextTools(){
 ++extraScenarios;using namespace notes_text_tools;
 const auto empty=Count(L"");Check(empty.words==0&&empty.characters==0&&empty.lines==1,"An empty note has zero words/characters and one logical line");
 const auto mixed=Count(L"one two\r\nthree");Check(mixed.words==3&&mixed.characters==13&&mixed.lines==2,"CRLF is one character and one logical break in document statistics");
 const auto unicode=Count(L"hello \U0001f680 world");Check(unicode.words==3&&unicode.characters==13,"A supplementary Unicode symbol counts once without corrupting UTF-16 offsets");
 const auto spaces=Count(L"one\x3000two\x00a0three");Check(spaces.words==3,"Unicode ideographic and nonbreaking spaces separate words");
 const auto soft=Count(L"one\r\ntwo\vthree\nfour");Check(soft.words==4&&soft.characters==18&&soft.lines==4,"A native soft break separates words and logical lines while CRLF still counts once");
 Check(LineStart(L"one\r\ntwo\vthree\nfour",2)==5&&LineStart(L"one\r\ntwo\vthree\nfour",3)==9&&LineStart(L"one\r\ntwo\vthree\nfour",4)==15,"Mixed paragraph and soft breaks retain exact native UTF-16 navigation offsets");
 Check(LineStart(L"A\r\nB\nC\r",1)==0&&LineStart(L"A\r\nB\nC\r",2)==3&&LineStart(L"A\r\nB\nC\r",3)==5&&LineStart(L"A\r\nB\nC\r",4)==7,"Logical navigation supports mixed newline forms and an empty final line");
 Check(!LineStart(L"A",0)&&!LineStart(L"A",2)&&LineStart(L"",1)==0,"Invalid line numbers cannot target another character range");
 Check(ParseLine(L"12")==12&&!ParseLine(L"0")&&!ParseLine(L"-1")&&!ParseLine(L"12x")&&!ParseLine(L"99999999999"),"Line input rejects zero, signs, garbage and oversized values before navigation");
}
static void WrapPresentation(){
 const auto owner=ExtrasFixture(L"Wrap presentation",L"A long line ");auto &state=State(owner);const auto edit=Edit(owner);
 std::wstring tail;for(unsigned i=0;i<150;++i)tail+=L"wide content ";Type(owner,tail.c_str());Save(owner);Select(edit,5,15);
 const auto selection=ExtrasSelection(edit);const auto rtf=ExtrasRtf(edit);const auto disk=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes;const auto undo=SendMessageW(edit,EM_CANUNDO,0,0);
 Check(state.wordWrap&&SendMessageW(edit,EM_GETLINECOUNT,0,0)>1,"The normal native editor wraps a long logical line");
 Command(owner,IDC_NOTES_WORD_WRAP);
 Check(!state.wordWrap&&SendMessageW(edit,EM_GETLINECOUNT,0,0)==1,"Disabling wrapping displays one native line with horizontal navigation");
 Check((SendMessageW(edit,EM_GETOPTIONS,0,0)&ECO_AUTOHSCROLL)!=0,"Unwrapped editing uses native horizontal scrolling");
 Check(ExtrasRtf(edit)==rtf&&SameSelection(ExtrasSelection(edit),selection)&&SendMessageW(edit,EM_CANUNDO,0,0)==undo&&!SendMessageW(edit,EM_GETMODIFY,0,0),"Wrapping changes neither rich text, selection, dirty state nor Undo availability");
 Check(client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==disk,"A presentation preference never rewrites the client notebook");
 DestroyWindow(owner);const auto reopened=Open(L"Wrap presentation");Check(!State(reopened).wordWrap&&SendMessageW(Edit(reopened),EM_GETLINECOUNT,0,0)==1,"The wrap choice survives window close and normal reopening");
 for(const int width:{900,1200,940}){SetWindowPos(reopened,nullptr,0,0,width,600,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);LayoutClientNotes(reopened,GetDpiForWindow(reopened));Check(!State(reopened).wordWrap&&SendMessageW(Edit(reopened),EM_GETLINECOUNT,0,0)==1,"Normal background layout changes preserve unwrapped lines without altering the preference");}
 Command(reopened,IDC_NOTES_WORD_WRAP);Check(State(reopened).wordWrap&&SendMessageW(Edit(reopened),EM_GETLINECOUNT,0,0)>1,"Restoring wrapping reflows the same saved logical line");
 DestroyWindow(reopened);NotesBackgroundAudit("wrap persistence without document edits");
}
static void GoToAndInformation(){
 const auto owner=ExtrasFixture(L"Line navigation",L"first\r\nsecond \U0001f680\r\nthird");auto &state=State(owner);const auto edit=Edit(owner);const auto text=NotesRawEditorText(edit);Check(text.has_value(),"Logical tools read native character positions without WM_GETTEXT expansion");
 const auto rtf=ExtrasRtf(edit);const auto savedBefore=client_notes::ReadNotebook(state.clientRoot);
 std::wstring line=L"3";notesDialogObserver=[&](LPCWSTR resource,DLGPROC procedure,LPARAM data)->INT_PTR{Check(resource==MAKEINTRESOURCEW(IDD_NOTES_INPUT)&&procedure==NotesInputProc,"Go to line uses the compact existing native input dialog");reinterpret_cast<NotesInput *>(data)->first=line;return IDOK;};
 Command(owner,IDC_NOTES_GO_TO_LINE);const auto expected=notes_text_tools::LineStart(*text,3);Check(expected&&ExtrasSelection(edit).cpMin==static_cast<LONG>(*expected)&&ExtrasSelection(edit).cpMin==ExtrasSelection(edit).cpMax,"A chosen logical line lands at its exact native character offset after CRLF and emoji");
 const auto before=ExtrasSelection(edit);line=L"900000";Command(owner,IDC_NOTES_GO_TO_LINE);Check(SameSelection(ExtrasSelection(edit),before)&&lastNotice.find(L"existing line number")!=std::wstring::npos,"Out-of-range navigation keeps the previous selection and explains the valid range");
 notesDialogObserver={};Select(edit,0,5);Command(owner,IDC_NOTES_STATISTICS);Check(lastNotice.find(L"3 lines")!=std::wstring::npos&&lastNotice.find(L"5 selected characters")!=std::wstring::npos,"On-demand information reports logical lines and the exact selection");
 Command(owner,IDC_NOTES_DETAILS);Check(state.panel&&NotesWindowText(GetDlgItem(state.panel,IDC_NOTES_DETAILS_HINT)).find(L"characters")!=std::wstring::npos,"Details exposes document counts without a permanent footer");CloseNotesPanel(state);
 Check(ExtrasRtf(edit)==rtf,"Navigation and document information preserve the exact native rich text");
 auto savedAfter=client_notes::ReadNotebook(state.clientRoot);Check(savedAfter.status==client_notes::ReadStatus::Ok&&savedAfter.pages.size()==savedBefore.pages.size(),"Details retains the complete saved notebook");
 for(auto &page:savedAfter.pages){const auto prior=std::find_if(savedBefore.pages.begin(),savedBefore.pages.end(),[&](const auto &item){return item.id==page.id;});Check(prior!=savedBefore.pages.end(),"Information tools preserve every saved note identity");page.cursor=prior->cursor;page.scroll=prior->scroll;}
 std::string beforeBytes,afterBytes;Check(client_notes::SerializeNotebook(savedBefore.pages,beforeBytes)&&client_notes::SerializeNotebook(savedAfter.pages,afterBytes)&&beforeBytes==afterBytes,"Only remembered view positions may change when Details flushes the navigated caret; text, links, metadata and complete histories remain identical");
 const auto isolatedRoot=g_sDataDir/L"Sites"/L"Wrap presentation";const auto isolatedBefore=client_notes::ReadNotebook(isolatedRoot);Check(isolatedBefore.status==client_notes::ReadStatus::Ok,"The soft-line navigation review retains a separate client's saved notebook");
 Select(edit,LONG_MAX,LONG_MAX);const auto beforeCode=ExtrasRtf(edit);NotesCodeInput code;code.language=4;code.source=L"first \U0001f680 code\r\nsecond code\nthird code";
 Check(NotesInsertCode(owner,state,code),"A real native code block inserts mixed source newline forms through the production command");
 const auto codeText=NotesRawEditorText(edit);Check(codeText.has_value()&&std::count(codeText->begin(),codeText->end(),L'\v')==2,"The actual code table exposes exactly two native soft breaks for CRLF and LF source lines");
 const auto second=codeText->find(L"second code"),third=codeText->find(L"third code");Check(second!=std::wstring::npos&&third!=std::wstring::npos&&(*codeText)[second-1]==L'\v'&&(*codeText)[third-1]==L'\v',"Both later code lines begin immediately after real native soft-break characters");
 const auto codeRtf=ExtrasRtf(edit);const auto codeDisk=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes;const auto codeUndo=SendMessageW(edit,EM_CANUNDO,0,0);
 auto withoutSoft=*codeText;std::replace(withoutSoft.begin(),withoutSoft.end(),L'\v',L' ');
 Check(notes_text_tools::Count(*codeText).lines==notes_text_tools::Count(withoutSoft).lines+2,"Document information includes each explicit code line rather than only table paragraphs");
 notesDialogObserver=[&](LPCWSTR resource,DLGPROC procedure,LPARAM data)->INT_PTR{Check(resource==MAKEINTRESOURCEW(IDD_NOTES_INPUT)&&procedure==NotesInputProc,"Native code-line navigation uses the ordinary compact Go to line input");reinterpret_cast<NotesInput *>(data)->first=line;return IDOK;};
 for(const auto target:{second,third}){
  line=std::to_wstring(notes_text_tools::Count(std::wstring_view(*codeText).substr(0,target)).lines);Command(owner,IDC_NOTES_GO_TO_LINE);
  const auto caret=ExtrasSelection(edit);Check(caret.cpMin==static_cast<LONG>(target)&&caret.cpMax==caret.cpMin,"Go to line reaches the exact native code-line start after emoji and an explicit soft break");
 }
 notesDialogObserver={};
 Check(ExtrasRtf(edit)==codeRtf&&SendMessageW(edit,EM_CANUNDO,0,0)==codeUndo&&client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==codeDisk,"Reviewing and navigating code lines changes no source RTF, Undo availability or saved notebook bytes");
 SendMessageW(edit,EM_UNDO,0,0);Check(ExtrasRtf(edit)==beforeCode,"One Undo still removes the whole code insertion after logical-line navigation");
 Check(client_notes::ReadNotebook(isolatedRoot).notebook_snapshot.bytes==isolatedBefore.notebook_snapshot.bytes,"Code-line statistics, navigation and Undo leave the other client's notebook byte-for-byte unchanged");
 DestroyWindow(owner);NotesBackgroundAudit("logical line navigation and information");
}
struct MemoryPaper {
 HDC dc=nullptr;HBITMAP bitmap=nullptr;void *pixels=nullptr;
 MemoryPaper(){BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),1800,-1800,1,32,BI_RGB};dc=CreateCompatibleDC(nullptr);bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);Check(dc&&bitmap&&pixels,"A private bitmap provides the print fixture's only output device");SelectObject(dc,bitmap);RECT white{0,0,1800,1800};FillRect(dc,&white,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));}
 ~MemoryPaper(){if(dc)DeleteDC(dc);if(bitmap)DeleteObject(bitmap);}
};
static void PrintDocument(){
 const auto owner=ExtrasFixture(L"Print copy",L"Readable heading\r\nFirst paragraph with content.\r\nSecond paragraph.");auto &state=State(owner);const auto edit=Edit(owner);Select(edit,LONG_MAX,LONG_MAX);std::wstring continuation;for(unsigned line=0;line<300;++line)continuation+=L"\r\nA readable continuation line for real multi-page pagination.";Type(owner,continuation.c_str());Select(edit,0,16);Command(owner,IDC_NOTES_BOLD);Save(owner);const auto selection=ExtrasSelection(edit);const auto rtf=ExtrasRtf(edit);const auto disk=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes;
 MemoryPaper paper;notesPrintDialogObserver=[&](PRINTDLGW *choice)->BOOL{Check(choice->hwndOwner==owner&&(choice->Flags&PD_RETURNDC)&&!(choice->Flags&PD_NOSELECTION),"Print offers the current selection through the correct owner without showing a dialog");choice->hDC=paper.dc;choice->Flags|=PD_SELECTION;return TRUE;};
 const auto before=printStarts;Command(owner,IDC_NOTES_PRINT);paper.dc=nullptr;notesPrintDialogObserver={};
 Check(printStarts==before+1&&printPages>0&&printEnds>0&&!printAborts,"Real RichEdit pagination reaches only intercepted print-job boundaries");
 size_t ink=0;const auto pixels=static_cast<const DWORD *>(paper.pixels);for(size_t i=0;i<1800*1800;++i)if((pixels[i]&0xffffff)!=0xffffff)++ink;
 Check(ink>10,"The actual rich-text print copy renders visible ink onto white memory paper");
 MemoryPaper complete;const auto pagesBefore=printPages;notesPrintDialogObserver=[&](PRINTDLGW *choice)->BOOL{choice->hDC=complete.dc;choice->Flags&=~PD_SELECTION;return TRUE;};Command(owner,IDC_NOTES_PRINT);complete.dc=nullptr;notesPrintDialogObserver={};
 Check(printPages>pagesBefore+1&&printEnds==2&&!printAborts,"The full-note command paginates a long document through multiple real RichEdit pages without a printer or external job");
 Check(ExtrasRtf(edit)==rtf&&SameSelection(ExtrasSelection(edit),selection)&&client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==disk,"Printing preserves original formatting, selection and saved client notebook bytes");
 DestroyWindow(owner);NotesBackgroundAudit("print selection on private memory paper");
}
static void PrintRefusal(){
 const auto owner=ExtrasFixture(L"Print cancellation",L"Keep this text intact");const auto edit=Edit(owner);const auto rtf=ExtrasRtf(edit);const auto before=printStarts;
 notesPrintDialogObserver=[](PRINTDLGW *)->BOOL{return FALSE;};Command(owner,IDC_NOTES_PRINT);notesPrintDialogObserver={};Check(printStarts==before&&ExtrasRtf(edit)==rtf,"Canceling the print picker submits no job and changes no document");
 MemoryPaper paper;failPrintPage=true;const auto abortBefore=printAborts;Check(!NotesPrintRange(edit,paper.dc,{0,5},L"Private failure")&&printAborts==abortBefore+1,"A failed print page aborts the intercepted job and reports failure");failPrintPage=false;
 Check(ExtrasRtf(edit)==rtf,"A printing failure cannot affect the editor's rich text");DestroyWindow(owner);NotesBackgroundAudit("printing cancellation and failure");
}
int wmain(int argc,wchar_t **argv){if(argc!=2||wcscmp(argv[1],L"--headless-background")){std::cerr<<"Hidden selector required; no windows created\n";return 2;}try{
 backgroundNotesTests=true;NotesBackgroundEvents events;events.Start();SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);Gdiplus::GdiplusStartupInput graphics;Gdiplus::GdiplusStartup(&g_gdiplusToken,&graphics,nullptr);g_hInst=GetModuleHandleW(nullptr);g_bThemeIsDark=true;BuildThemeColors(true,g_themeColors);UpdateThemeBrushes();
 g_sDataDir=fs::current_path()/L"build"/(L"editing-extras-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));Check(fs::create_directories(g_sDataDir),"Fresh private editing-tools data root");g_sConfigPath=g_sDataDir/L"config.ini";WritePrivateProfileStringW(L"notes_window",L"help_seen",L"1",g_sConfigPath.c_str());g_hGui=CreateWindowExW(0,L"STATIC",L"Private editing parent",WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,g_hInst,nullptr);const auto decoder=LoadLibraryW(L"Msftedit.dll");Check(decoder!=nullptr,"The native rich-text decoder remains retained by the private fixture");
 TextTools();WrapPresentation();GoToAndInformation();PrintDocument();PrintRefusal();DestroyWindow(g_hGui);g_hGui=nullptr;NotesBackgroundAudit("final editing tools audit");NotesBackgroundBanner();FreeLibrary(decoder);std::cout<<"Notes editing tools: "<<checks<<" checks; "<<extraScenarios<<" scenarios; 0 failures\n";return 0;
 }catch(const std::exception &error){std::cerr<<"FAIL editing tools: "<<error.what()<<std::endl;NotesBackgroundBanner();return 1;}}
