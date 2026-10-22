#ifdef CTSPACES_LINK_CONTEXT_BASELINE
#define CTSPACES_NOTES_PRODUCTION_SOURCE "../build/notes-link-context-2026-10-16/ctSpaces-before.cpp"
#endif
#define wmain ExistingNotesWorkspaceEntryNotRun
#include "NotesWorkspaceTests.cpp"
#undef wmain
#include "RetainedFixture.h"
#include "NotesLinkContextTests.inl"
int wmain(){try{
 std::cout<<std::unitbuf;backgroundNotesTests=true;NotesBackgroundEvents events;events.Start();notesCaptureClipboard=true;
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
 Gdiplus::GdiplusStartupInput graphics;Gdiplus::GdiplusStartup(&g_gdiplusToken,&graphics,nullptr);
 g_hInst=GetModuleHandleW(nullptr);g_bThemeIsDark=true;BuildThemeColors(true,g_themeColors);UpdateThemeBrushes();
 g_sDataDir=retained_fixture::CreateRoot(fs::current_path()/L"build",L"link-context");Check(fs::is_directory(g_sDataDir),"Fresh private link-context fixture");g_sConfigPath=g_sDataDir/L"config.ini";
 g_hGui=notes_test::CreateNoteWindow(0,L"STATIC",L"Hidden link context parent",WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,g_hInst,nullptr);
 NotesLinkContextCases();NotesLinkContextMisses();NotesLinkContextGlyphCoverage();DestroyWindow(g_hGui);g_hGui=nullptr;NotesBackgroundAudit("link context suite cleanup");NotesBackgroundBanner();
#ifdef CTSPACES_LINK_CONTEXT_DIAGNOSTIC
 std::cout<<"Notes link context DIAGNOSTIC ONLY: "<<checks<<" checks, "<<linkContextCases<<" cases, "<<linkContextDiagnosticMisses<<" unmet expectations; actual input/foreground/clipboard/browser=0.\n";
#else
 std::cout<<"Notes link context: "<<checks<<" checks, "<<linkContextCases<<" cases, 0 failures; actual input/foreground/clipboard/browser=0.\n";
#endif
 notesCaptureClipboard=false;
 Gdiplus::GdiplusShutdown(g_gdiplusToken);CoUninitialize();return 0;
}catch(const std::exception &error){std::cerr<<"Link context failure after "<<checks<<" checks: "<<error.what()<<'\n';NotesBackgroundBanner();return 1;}}

