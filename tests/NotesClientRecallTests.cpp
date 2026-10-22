// Reuse the production Notes fixture and its process-private background guard.
// The historical entrypoint is compiled but never executed by this suite.
#define wmain HistoricalNotesWorkspaceEntryNotRun
#include "NotesWorkspaceTests.cpp"
#undef wmain
#include "RetainedFixture.h"

static unsigned recallChecks=0,recallFailures=0;
static void RecallCheck(bool value,const char *description){++recallChecks;if(!value)++recallFailures;std::cout<<(value?"PASS ":"FAIL ")<<description<<'\n';}
static void SeedRecallNote(const std::wstring &name,const std::wstring &text){
 Fixture(name);const auto root=g_sDataDir/L"Sites"/name;
 client_notes::NotePage page{L"Client identity",true,NotesPlainRtf(text),{}};page.id=1;
 Check(client_notes::WriteNotebook(root,{page},client_notes::ReadNotebook(root)),"Seed exact private client notebook");
}
static void SearchExactLongClient(HWND notes){
 const std::wstring prefix(64,L'Q');
 std::wstring ninth;
 for(unsigned i=1;i<=9;++i){
  const auto name=prefix+L"-0"+std::to_wstring(i);Fixture(name);if(i==9)ninth=name;
 }
 NotesShowPanel(notes,&State(notes),false);
 const auto panel=State(notes).panel;
 Check(panel && IsWindow(panel),"Native client switch panel opens against private client fixtures");
 auto *data=reinterpret_cast<NotesPanel *>(GetWindowLongPtrW(panel,DWLP_USER));
 const auto edit=GetDlgItem(panel,IDC_NOTES_CLIENT_SEARCH);
 SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(prefix.c_str()));
 RecallCheck(data && data->matches.size()==8 && std::find(data->matches.begin(),data->matches.end(),ninth)==data->matches.end(),"Shared 64-character search prefix returns eight rows and omits the ninth client");
 SendMessageW(edit,EM_SETSEL,0,-1);
 SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(ninth.c_str()));
 RecallCheck(NotesWindowText(edit)==ninth,"Native client-switch input retains the full client name after its 64-character shared prefix");
 RecallCheck(data && data->matches.size()==1 && data->matches.front()==ninth,"Typing the full ninth client name finds its exact client instead of the first eight prefix matches");
 const std::wstring longest(kMaxClientNameLength,L'X');
 SendMessageW(edit,EM_SETSEL,0,-1);
 SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(longest.c_str()));
 RecallCheck(NotesWindowText(edit)==longest,"Native client-switch input accepts the supported 240-character identifier boundary");
 SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"Y"));
 RecallCheck(NotesWindowText(edit)==longest,"Native client-switch input does not accept identifiers beyond the supported maximum");
 CloseNotesPanel(State(notes));
 // The move/copy destination uses an editable native combo, which has its
 // own text limit. SetWindowText would bypass that native user-input limit.
 NotesClientPicker picker{State(notes).clientName,L"",false};
 const auto destination=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_CLIENT_PICKER),notes,NotesClientPickerProc,reinterpret_cast<LPARAM>(&picker));
 Check(destination && IsWindow(destination),"Native move/copy destination picker opens against private clients");
 const auto combo=GetDlgItem(destination,IDC_NOTES_CLIENT_PICKER);
 COMBOBOXINFO info{sizeof(info)};Check(GetComboBoxInfo(combo,&info) && info.hwndItem,"Native destination combo exposes its editable child");
 // Keep the suggestion routine in its existing loading state while observing
 // native text limits: a real dropped combo list is outside background scope.
 picker.updating=true;
 SendMessageW(info.hwndItem,EM_SETSEL,0,-1);
 SendMessageW(info.hwndItem,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(ninth.c_str()));
 picker.updating=false;
 RecallCheck(NotesWindowText(combo)==ninth,"Native move/copy destination typing retains the full long client identifier");
 SendMessageW(destination,WM_COMMAND,IDOK,0);
 RecallCheck(picker.selected==ninth,"A typed full long client identifier resolves to the exact move/copy destination");
 DestroyWindow(destination);
}
static void SearchExactNameBeforePinnedPrefixes(HWND notes){
 const std::wstring target=L"Acme";Fixture(target);
 const auto pins=g_pinnedClients;
 g_pinnedClients.clear();
 for(unsigned i=1;i<=8;++i){const auto name=target+L" division "+std::to_wstring(i);Fixture(name);g_pinnedClients.push_back(name);}
 NotesShowPanel(notes,&State(notes),false);
 const auto panel=State(notes).panel;
 Check(panel && IsWindow(panel),"Native switch panel opens with eight pinned prefix matches plus an exact unpinned client");
 auto *data=reinterpret_cast<NotesPanel *>(GetWindowLongPtrW(panel,DWLP_USER));
 const auto edit=GetDlgItem(panel,IDC_NOTES_CLIENT_SEARCH);
 SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"acme"));
 RecallCheck(data && !data->matches.empty() && data->matches.front()==target,"A full case-insensitive client-name query puts its exact client ahead of eight pinned prefix matches");
 CloseNotesPanel(State(notes));g_pinnedClients=pins;
}
int wmain(){try{
 std::cout<<std::unitbuf;backgroundNotesTests=true;NotesBackgroundEvents events;events.Start();
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
 Gdiplus::GdiplusStartupInput graphics;Gdiplus::GdiplusStartup(&g_gdiplusToken,&graphics,nullptr);
 g_hInst=GetModuleHandleW(nullptr);g_bThemeIsDark=true;BuildThemeColors(true,g_themeColors);UpdateThemeBrushes();
 g_sDataDir=retained_fixture::CreateRoot(fs::current_path()/L"build",L"n10recall");
 Check(fs::is_directory(g_sDataDir),"Fresh private recall fixture");g_sConfigPath=g_sDataDir/L"config.ini";
 g_hGui=notes_test::CreateNoteWindow(0,L"STATIC",L"Private recall parent",WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,g_hInst,nullptr);
 const std::wstring prefix(64,L'P'),longName=prefix+L"-Production \u00C5ngstr\u00F6m",shortName=L"Ordinary client";
 SeedRecallNote(prefix,L"SHORT PREFIX CLIENT NOTES");SeedRecallNote(longName,L"LONG CLIENT NOTES");SeedRecallNote(shortName,L"ORDINARY NOTES");
 const auto prefixBefore=client_notes::ReadNotebook(g_sDataDir/L"Sites"/prefix).notebook_snapshot.bytes;
 const auto longBefore=client_notes::ReadNotebook(g_sDataDir/L"Sites"/longName).notebook_snapshot.bytes;
 for(const auto &name:std::vector<std::wstring>{shortName,prefix,longName,std::wstring(240,L'X')}){
  NotesRememberClient(name);const auto recent=NotesRecentClients();
  RecallCheck(!recent.empty()&&recent.front()==name,"Remembered Notes client names round-trip at ordinary, 64, over-64 and 240 UTF-16 lengths");
 }
 NotesRememberClient(longName);g_sessions.clear();g_iSelectedSessionTab=0;g_bUiEnabled=true;g_bClientNotesBusy=false;
 ShowNotesFromMain();
 RecallCheck(g_hNotesWindow&&State(g_hNotesWindow).clientName==longName,"Main Notes button reopens the exact remembered long client instead of its existing 64-character prefix");
 RecallCheck(g_hNotesWindow&&NotesWindowText(Edit(g_hNotesWindow)).find(L"LONG CLIENT NOTES")!=std::wstring::npos,"Main Notes button loads the remembered client's own note text");
 if(g_hNotesWindow)DestroyWindow(g_hNotesWindow);
 // Repeat the persisted lookup after window destruction and clearing UI state.
 g_sessions.clear();g_iSelectedSessionTab=0;NotesRememberClient(longName);g_allClientNames.clear();
 ShowNotesFromMain();RecallCheck(g_hNotesWindow&&State(g_hNotesWindow).clientName==longName,"Repeated remembered-client lookup retains the exact client identity after the window is destroyed");
 if(g_hNotesWindow){SearchExactLongClient(g_hNotesWindow);SearchExactNameBeforePinnedPrefixes(g_hNotesWindow);}
 if(g_hNotesWindow)DestroyWindow(g_hNotesWindow);
 const auto recent=NotesRecentClients();RecallCheck(std::count(recent.begin(),recent.end(),longName)==1,"Repeated long-client visits deduplicate the full name");
 RecallCheck(client_notes::ReadNotebook(g_sDataDir/L"Sites"/prefix).notebook_snapshot.bytes==prefixBefore,"Opening recalled Notes never mutates the independent prefix-client notebook");
 RecallCheck(client_notes::ReadNotebook(g_sDataDir/L"Sites"/longName).notebook_snapshot.bytes==longBefore,"Opening recalled Notes preserves exact saved long-client notebook bytes");
 DestroyWindow(g_hGui);g_hGui=nullptr;NotesBackgroundAudit("client recall cleanup");NotesBackgroundBanner();
 std::cout<<"Notes client recall: "<<recallChecks<<" checks, 10 scenarios, "<<recallFailures<<" failures; actual input/foreground/clipboard=0.\n";
 Gdiplus::GdiplusShutdown(g_gdiplusToken);CoUninitialize();return recallFailures?1:0;
}catch(const std::exception &error){std::cerr<<"Recall fixture failed: "<<error.what()<<'\n';NotesBackgroundBanner();return 2;}}
