#define wmain NotesWorkspaceUnusedMain
#include "NotesWorkspaceTests.cpp"
#undef wmain
#include "../NotesMarkdown.h"

namespace markdown_test {
static unsigned groups=0;
static LRESULT CALLBACK RefuseActivation(int code,WPARAM w,LPARAM l) {
  if(code==HCBT_ACTIVATE || code==HCBT_SETFOCUS)return 1;
  return CallNextHookEx(nullptr,code,w,l);
}
#include "NotesMarkdownCoreTests.inl"
static bool ExactNativeMarkdownPreview(HWND owner,std::wstring_view input) {
  const auto rendered=notes_markdown::Render(notes_markdown::Parse(input).document);client_notes::NotePage page;std::wstring error;
  if(!NotesMarkdownPage(rendered,L"Exact native preview",page,error))return false;
  std::vector<NotesLinkPiece> pieces;const LONG prospective=NotesReplacementStoryLength(owner,page,pieces);
  HWND decoder=CreateWindowExW(0,L"RICHEDIT50W",L"",WS_CHILD|ES_MULTILINE,0,0,1,1,owner,nullptr,g_hInst,nullptr);
  if(!decoder)return false;
  NotesStream stream{page.rtf};EDITSTREAM source{reinterpret_cast<DWORD_PTR>(&stream),0,NotesStreamIn};SendMessageW(decoder,EM_STREAMIN,SF_RTF,reinterpret_cast<LPARAM>(&source));
  Microsoft::WRL::ComPtr<IUnknown> unknown;Microsoft::WRL::ComPtr<ITextDocument> document;Microsoft::WRL::ComPtr<ITextRange> range;
  SendMessageW(decoder,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(unknown.GetAddressOf()));
  bool exact=false;
  if(unknown && SUCCEEDED(unknown.As(&document)) && SUCCEEDED(document->Range(0,prospective,&range))) {
    BSTR raw=nullptr;if(SUCCEEDED(range->GetText(&raw))) {
      std::wstring actual(raw?raw:L"",raw?SysStringLen(raw):0);SysFreeString(raw);
      // A fenced source block deliberately uses native soft breaks. Compare
      // their readable line separators while retaining all positions exactly.
      std::replace(actual.begin(),actual.end(),L'\v',L'\r');
      exact=!source.dwError && prospective==static_cast<LONG>(rendered.plain.size()) && actual==rendered.plain && pieces.size()==rendered.links.size();
      for(size_t i=0;exact && i<pieces.size();++i)exact=pieces[i].first==rendered.links[i].first && pieces[i].last==rendered.links[i].last && pieces[i].label==rendered.links[i].label && pieces[i].url==rendered.links[i].url;
    }
  }
  DestroyWindow(decoder);return exact;
}
static void ImportAndExport(const fs::path &artifacts) {
  ++groups;Fixture(L"Markdown Alpha");Fixture(L"Markdown Beta");
  HWND owner=Open(L"Markdown Alpha");auto &state=State(owner);
  if(!HasActiveNote(state))Command(owner,IDC_NOTES_ADD_TAB);
  Type(owner,L"Original Alpha content");Save(owner);
  state.pages[state.activePage].ticket=L"T20261019.0622";state.pages[state.activePage].status=2;state.pages[state.activePage].tags={L"keep"};state.metadataChanged=true;Save(owner);
  const auto originalId=state.pages[state.activePage].id,originalPages=state.pages.size();
  const auto betaBefore=client_notes::ReadNotebook(g_sDataDir/L"Sites/Markdown Beta").notebook_snapshot.bytes;
  const fs::path source=artifacts/L"import.md";
  Put(source,"# Imported title\n\nA **strong** [portal](https://example.test/path?q=1).\n- [ ] Follow up\n```powershell\nGet-Printer\n```\n");
  Check(ExactNativeMarkdownPreview(owner,L"# Imported title\n\nA **strong** [portal](https://example.test/path?q=1).\n- [ ] Follow up\n```powershell\nGet-Printer\n```\n"),
        "Native preview preserves every paragraph, explicit final newline and exact labeled-link offsets");
  answers.push_back(IDCANCEL);
  Check(!NotesImportMarkdownFile(owner,state,source) && state.pages.size()==originalPages,"Canceling Markdown preview does not add or replace a note");
  answers.push_back(IDOK);
  Check(NotesImportMarkdownFile(owner,state,source) && state.pages.size()==originalPages+1,"Markdown confirmation creates a separate note in the current client");
  const auto &imported=state.pages[state.activePage];
  Check(imported.id!=originalId && imported.ticket.empty() && imported.tags.empty() && imported.status==0 && imported.history.empty() && !imported.links.empty() && imported.links[0].second==L"https://example.test/path?q=1",
        "Imported content receives independent note identity and validated web links without inheriting ticket metadata or history");
  const auto original=std::find_if(state.pages.begin(),state.pages.end(),[&](const auto &page){return page.id==originalId;});
  Check(original!=state.pages.end() && original->ticket==L"T20261019.0622" && original->status==2 && original->tags==std::vector<std::wstring>{L"keep"} && client_notes::ReadNotebook(g_sDataDir/L"Sites/Markdown Beta").notebook_snapshot.bytes==betaBefore,
        "Markdown import preserves the original note metadata and the unrelated client's notebook bytes");
  answers.push_back(IDCANCEL);const auto captured=client_notes::ReadNotebook(g_sDataDir/L"Sites/Markdown Alpha").notebook_snapshot.bytes;
  Check(!NotesExportMarkdown(owner,state) && files.empty() && lastNotice.find(L"not preserved")!=std::wstring::npos,"Markdown text export requires explicit approval of its formatting-loss preview before a file picker");
  answers.push_back(IDOK);const fs::path output=artifacts/L"export.md";files.push_back(output.wstring());
  Check(NotesExportMarkdown(owner,state) && files.empty(),"Explicit Markdown text export writes a separate atomic UTF-8 copy");
  const auto exported=client_notes::ReadFileSnapshot(artifacts,L"export.md",client_notes::kMaxBytes);std::wstring decoded;
  Check(exported.status==client_notes::ReadStatus::Ok && client_notes::DecodeUtf8(exported.bytes,decoded) && decoded.find(L"https")!=std::wstring::npos && decoded.find(L"Web links")!=std::wstring::npos,
        "Markdown text export retains link destinations in its disclosed web-link appendix");
  Check(client_notes::ReadNotebook(g_sDataDir/L"Sites/Markdown Alpha").notebook_snapshot.bytes==captured,"Markdown export does not alter the saved client notebook or metadata");
  DestroyWindow(owner);NotesBackgroundAudit("Markdown import preview, separate-note identity and explicit text export");
}
#include "NotesMarkdownRichTests.inl"
#include "NotesMarkdownLevelsTests.inl"
#include "NotesMarkdownBudgetTests.inl"
#include "NotesMarkdownFilledTablesTests.inl"
static void Tables() {
  ++groups;Fixture(L"Native table Alpha");HWND owner=Open(L"Native table Alpha");auto &state=State(owner);
  if(!HasActiveNote(state))Command(owner,IDC_NOTES_ADD_TAB);
  Type(owner,L"Before table\r\n");Save(owner);HWND edit=Edit(owner);
  std::string before;StreamClientNotesOut(edit,before);Select(edit,-1,-1);
  Check(NotesInsertSimpleTable(owner,state,3,3),"Insert table uses the real native three-by-three insertion and same-client autosave");
  const auto saved=client_notes::ReadNotebook(g_sDataDir/L"Sites/Native table Alpha");
  Check(saved.status==client_notes::ReadStatus::Ok && saved.pages[state.activePage].rtf.find("\\trowd")!=std::string::npos,"Native table rows survive the client notebook round-trip");
  LONG first=-1;const LONG length=GetWindowTextLengthW(edit);
  for(LONG p=0;p<=length;++p){Select(edit,p,p);CHARRANGE accepted{};NotesSelection(edit,accepted);PARAFORMAT2 paragraph{sizeof(paragraph)};SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));if((paragraph.wEffects&PFE_TABLE) && !(paragraph.wEffects&PFE_TABLEROWDELIMITER) && accepted.cpMin==accepted.cpMax){first=accepted.cpMin;break;}}
  Check(first>=0,"The inserted native table exposes an editable cell through paragraph metadata");
  Select(edit,first,first);CHARRANGE prior{},next{};NotesSelection(edit,prior);
  MSG tabMessage{};tabMessage.hwnd=edit;tabMessage.message=WM_KEYDOWN;tabMessage.wParam=VK_TAB;
  const auto dialogCode=SendMessageW(edit,WM_GETDLGCODE,VK_TAB,reinterpret_cast<LPARAM>(&tabMessage));
  Check((dialogCode&(DLGC_WANTTAB|DLGC_WANTALLKEYS))!=0,"The native editor asks the real dialog manager to route Tab into the table instead of another control");
  PARAFORMAT2 beforeParagraph{sizeof(beforeParagraph)};SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&beforeParagraph));
  std::string beforeTab,afterTab;StreamClientNotesOut(edit,beforeTab);
  SendMessageW(edit,WM_KEYDOWN,VK_TAB,0);CHARRANGE afterKeyDown{};NotesSelection(edit,afterKeyDown);
  SendMessageW(edit,WM_CHAR,VK_TAB,0);SendMessageW(edit,WM_KEYUP,VK_TAB,0);NotesSelection(edit,next);
  StreamClientNotesOut(edit,afterTab);
  PARAFORMAT2 afterParagraph{sizeof(afterParagraph)};SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&afterParagraph));
  std::cout<<"Native table Tab: requested="<<first<<" prior="<<prior.cpMin<<","<<prior.cpMax<<" afterKeyDown="<<afterKeyDown.cpMin<<","<<afterKeyDown.cpMax<<" next="<<next.cpMin<<","<<next.cpMax
    <<" rtfUnchanged="<<(beforeTab==afterTab)<<" beforeBytes="<<beforeTab.size()<<" afterBytes="<<afterTab.size()<<" numbering="<<beforeParagraph.wNumbering<<"/"<<afterParagraph.wNumbering
    <<" effects="<<beforeParagraph.wEffects<<"/"<<afterParagraph.wEffects<<" style="<<GetWindowLongPtrW(edit,GWL_STYLE)<<" exstyle="<<GetWindowLongPtrW(edit,GWL_EXSTYLE)
    <<" shift="<<(GetKeyState(VK_SHIFT)&0x8000)<<" control="<<(GetKeyState(VK_CONTROL)&0x8000)<<" enabled="<<IsWindowEnabled(edit)<<"/"<<IsWindowEnabled(owner)<<"\n";
  Put(g_sDataDir/L"table-before-tab.rtf",beforeTab);Put(g_sDataDir/L"table-after-tab.rtf",afterTab);
  Check(next.cpMin!=prior.cpMin,"The actual translated RichEdit Tab sequence moves the caret into another table cell");
  Check(afterTab==beforeTab,"Table cell navigation neither inserts text nor changes the complete native RTF");
  const bool previousCell=NotesNavigateTableCell(edit,true);CHARRANGE previous{};NotesSelection(edit,previous);
  std::string afterPrevious;StreamClientNotesOut(edit,afterPrevious);
  Check(previousCell && previous.cpMin==prior.cpMin && previous.cpMax==prior.cpMax && afterPrevious==beforeTab,
        "Native TOM previous-cell navigation returns to the prior cell without key-state access or document mutation");
  const bool firstBoundary=NotesNavigateTableCell(edit,true);CHARRANGE firstBoundaryRange{};NotesSelection(edit,firstBoundaryRange);
  Check(firstBoundary && firstBoundaryRange.cpMin==prior.cpMin && firstBoundaryRange.cpMax==prior.cpMax,
        "Previous cell at the first table cell is handled without leaving the table");
  Microsoft::WRL::ComPtr<IUnknown> unknown;Microsoft::WRL::ComPtr<ITextDocument> document;Microsoft::WRL::ComPtr<ITextRange> cell;
  SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(unknown.GetAddressOf()));LONG last=-1;
  const bool lastCell=unknown && SUCCEEDED(unknown.As(&document)) && (last=NativeTableCellStart(document.Get(),8))>=0 &&
    SUCCEEDED(document->Range(last,last,&cell));
  Select(edit,last,last);CHARRANGE lastBefore{},lastAfter{};NotesSelection(edit,lastBefore);const bool lastBoundary=NotesNavigateTableCell(edit,false);NotesSelection(edit,lastAfter);
  std::string afterBoundary;StreamClientNotesOut(edit,afterBoundary);
  Check(lastCell && lastBoundary && lastBefore.cpMin==last && lastAfter.cpMin==last && afterBoundary==beforeTab &&
        client_notes::ReadNotebook(g_sDataDir/L"Sites/Native table Alpha").notebook_snapshot.bytes==saved.notebook_snapshot.bytes,
        "Next cell at the final table boundary adds no row, changes no RTF and writes no notebook");
  Select(edit,0,0);Check(!NotesNavigateTableCell(edit,false),"Normal paragraphs keep their existing Tab and list handling");Select(edit,prior.cpMin,prior.cpMax);
  Check(!NotesInsertSimpleTable(owner,state,3,3),"Insert table refuses nesting inside a table or code box");
  SendMessageW(edit,EM_UNDO,0,0);std::string undone;StreamClientNotesOut(edit,undone);
  Check(undone==before,"One native Undo restores the complete document from before table insertion");
  // The owner releases its Msftedit module during destruction. Release every
  // table diagnostic TOM interface while that native editor is still alive.
  cell.Reset();document.Reset();unknown.Reset();
  DestroyWindow(owner);NotesBackgroundAudit("native table insertion, keyboard cell navigation, nesting refusal and single Undo");
}
}
int wmain(int argc,wchar_t **argv) {
  if(argc!=3 || wcscmp(argv[1],L"--headless-background")){std::cerr<<"Hidden entry and private artifact path required.\n";return 2;}
  try {
    backgroundNotesTests=true;NotesBackgroundEvents events;events.Start();
    const auto hook=SetWindowsHookExW(WH_CBT,markdown_test::RefuseActivation,nullptr,GetCurrentThreadId());Check(hook!=nullptr,"Native activation and focus are vetoed before desktop interaction");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);Gdiplus::GdiplusStartupInput graphics;Gdiplus::GdiplusStartup(&g_gdiplusToken,&graphics,nullptr);
    g_hInst=GetModuleHandleW(nullptr);g_bThemeIsDark=true;BuildThemeColors(true,g_themeColors);UpdateThemeBrushes();
    g_sDataDir=fs::current_path()/L"build"/(L"markdown-private-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));fs::create_directories(g_sDataDir);g_sConfigPath=g_sDataDir/L"ctSpaces.ini";
    const fs::path artifacts=argv[2];fs::create_directories(artifacts);
    g_hGui=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Private Markdown parent",WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,g_hInst,nullptr);
    markdown_test::Core();markdown_test::ImportAndExport(artifacts);markdown_test::RichSnapshotAndContexts(artifacts);markdown_test::ImportedHeadingAndListDisclosure(artifacts);markdown_test::NativeMetadataBudget(artifacts);markdown_test::Tables();markdown_test::FilledTableNavigation();
    DestroyWindow(g_hGui);g_hGui=nullptr;NotesBackgroundAudit("final Markdown cleanup");NotesBackgroundBanner();UnhookWindowsHookEx(hook);
    std::cout<<"Markdown interchange: "<<checks<<" checks; "<<markdown_test::groups<<" private groups; 0 failures.\n";return 0;
  }catch(const std::exception &error){std::cerr<<"FAIL: "<<error.what()<<std::endl;NotesBackgroundBanner();return 1;}
}
