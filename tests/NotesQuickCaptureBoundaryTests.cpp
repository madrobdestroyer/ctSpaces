// Real quick-capture data saves behind the existing process-private native UI
// guard. No historical main, user input, clipboard or application launch runs.
#define wmain HistoricalNotesWorkspaceEntryNotRun
#include "NotesWorkspaceTests.cpp"
#undef wmain
#include "RetainedFixture.h"

static unsigned boundaryChecks=0,boundaryFailures=0;
static void BoundaryCheck(bool ok,const char *label) {
  ++boundaryChecks;if(!ok)++boundaryFailures;
  std::cout<<(ok?"PASS ":"FAIL ")<<label<<std::endl;
}
static void QuickCaptureNaming(const std::wstring &client,const std::wstring &title,bool collides) {
  Fixture(client);const auto root=g_sDataDir/L"Sites"/client;
  client_notes::NotePage original{collides?title:L"Independent note",true,NotesPlainRtf(L"ORIGINAL PROTECTED CONTENT"),{}};original.id=1;
  Check(client_notes::ValidPageName(title),"The entered quick-capture title is valid before collision suffixing");
  Check(client_notes::WriteNotebook(root,{original},client_notes::ReadNotebook(root)),"Private quick-capture client starts from a real saved notebook");
  NotesQuickState state{client,root,client_notes::ReadNotebook(root)};state.pages=state.baseline.pages;
  HWND dialog=notes_test::Modeless(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_QUICK),g_hGui,NotesQuickProc,reinterpret_cast<LPARAM>(&state));
  Check(dialog!=nullptr,"Actual hidden native quick-capture dialog is created");
  const std::wstring text=L"UNIQUE QUICK CAPTURE WORK \u4e2d\u6587 \xD83D\xDE80";
  SetDlgItemTextW(dialog,IDC_NOTES_QUICK_TITLE,title.c_str());SetDlgItemTextW(dialog,IDC_NOTES_QUICK_TEXT,text.c_str());
  BoundaryCheck(NotesQuickSave(dialog,state),"A valid Unicode title can autosave when its existing title needs a collision suffix");
  auto saved=client_notes::ReadNotebook(root);
  BoundaryCheck(saved.status==client_notes::ReadStatus::Ok && saved.pages.size()==2 && NotesPlain(saved.pages.front())==L"ORIGINAL PROTECTED CONTENT","Quick capture retains the original independent note and appends exactly one saved note");
  BoundaryCheck(saved.pages.size()==2 && client_notes::ValidPageName(saved.pages.back().name) && NotesPlain(saved.pages.back())==text,"New capture content and generated Unicode title are both durably valid");
  const auto firstId=state.id;
  SetDlgItemTextW(dialog,IDC_NOTES_QUICK_TEXT,(text+L"\r\nA second pending update").c_str());
  BoundaryCheck(NotesQuickSave(dialog,state) && state.id==firstId && state.id!=0,"A second autosave updates the same capture identity instead of creating another note");
  saved=client_notes::ReadNotebook(root);
  BoundaryCheck(saved.pages.size()==2 && NotesPlain(saved.pages.back())==text+L"\r\nA second pending update","A second quick capture save keeps both exact notes and the new text");
  BoundaryCheck(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_QUICK_TEXT))==text+L"\r\nA second pending update","Successful or refused capture saves never replace the editable draft");
  DestroyWindow(dialog);NotesBackgroundAudit("quick capture Unicode collision");
}
int wmain(){try {
  std::cout<<std::unitbuf;backgroundNotesTests=true;NotesBackgroundEvents events;events.Start();
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
  Gdiplus::GdiplusStartupInput graphics;Gdiplus::GdiplusStartup(&g_gdiplusToken,&graphics,nullptr);
  g_hInst=GetModuleHandleW(nullptr);g_bThemeIsDark=true;BuildThemeColors(true,g_themeColors);UpdateThemeBrushes();
  Check(LoadLibraryW(L"Msftedit.dll")!=nullptr,"Native rich-text decoder is available for exact saved content checks");
  g_sDataDir=retained_fixture::CreateRoot(fs::current_path()/L"build",L"n11quick");
  Check(fs::is_directory(g_sDataDir),"Fresh process-private quick capture fixture");g_sConfigPath=g_sDataDir/L"config.ini";
  g_hGui=notes_test::CreateNoteWindow(0,L"STATIC",L"Private quick capture parent",WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,g_hInst,nullptr);
  QuickCaptureNaming(L"Quick split Unicode",std::wstring(51,L'A')+L"\xD83D\xDE80"+L" task",true);
  QuickCaptureNaming(L"Quick intact Unicode",std::wstring(50,L'B')+L"\xD83D\xDE80"+L" task",true);
  QuickCaptureNaming(L"Quick no collision",std::wstring(51,L'C')+L"\xD83D\xDE80"+L" task",false);
  QuickCaptureNaming(L"Quick ordinary collision",L"Ordinary capture title",true);
  DestroyWindow(g_hGui);g_hGui=nullptr;NotesBackgroundAudit("quick capture cleanup");NotesBackgroundBanner();
  std::cout<<"Notes quick capture boundaries: "<<boundaryChecks<<" checks, 4 scenarios, "<<boundaryFailures<<" failures; actual input/foreground/clipboard=0.\n";
  Gdiplus::GdiplusShutdown(g_gdiplusToken);CoUninitialize();return boundaryFailures?1:0;
}catch(const std::exception &error){std::cerr<<"Quick capture fixture failed: "<<error.what()<<'\n';NotesBackgroundBanner();return 2;}}
