// Round 3: independent client/library fixtures, native editors and real atomic
// notebook writes. Commit hooks simulate the narrow competing-writer window.
namespace notes_persistence {
struct FixtureScope {
  fs::path data=g_sDataDir,config=g_sConfigPath;
  FixtureScope() {g_sDataDir=data/L"PersistenceRound3";fs::create_directories(g_sDataDir);g_sConfigPath=g_sDataDir/L"ctSpaces.ini";}
  ~FixtureScope() {afterNoteCommit={};answers.clear();inputs.clear();if(g_hNotesWindow && IsWindow(g_hNotesWindow)) DestroyWindow(g_hNotesWindow);g_sDataDir=data;g_sConfigPath=config;}
};
static LRESULT CALLBACK FailCapture(HWND edit,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
  if(message==WM_NCDESTROY) RemoveWindowSubclass(edit,FailCapture,id);
  if(message==EM_STREAMOUT && *reinterpret_cast<unsigned *>(data)) {--*reinterpret_cast<unsigned *>(data);reinterpret_cast<EDITSTREAM *>(l)->dwError=ERROR_NOT_ENOUGH_MEMORY;return 0;}
  return DefSubclassProc(edit,message,w,l);
}
static void Link(HWND owner,const std::wstring &label,const std::wstring &uri,LONG first,LONG last) {
  Select(Edit(owner),first,last);inputs.push_back({label,uri});Command(owner,IDC_NOTES_LINK);Save(owner);
}
static void Click(HWND owner,LONG first,LONG last,const std::wstring &uri) {
  const auto before=shellOpens;answers.push_back(IDYES);ENLINK event{{Edit(owner),IDC_CLIENT_NOTES_TEXT,EN_LINK},WM_LBUTTONUP,0,0,{first,last}};
  SendMessageW(owner,WM_NOTIFY,IDC_CLIENT_NOTES_TEXT,reinterpret_cast<LPARAM>(&event));
  Check(shellOpens==before+1 && (shellTarget==uri || shellParameters==QuoteCommandLineArgument(uri)),"Persisted native range opens the intended URL in the ordinary browser route");
}
static void SnippetLinks() {
  Fixture(L"Persistence snippet source");HWND owner=Open(L"Persistence snippet source");Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Support");Link(owner,L"Support",L"https://example.test/snippet",0,7);
  auto snippet=NotesSnippetFromSelection(owner,State(owner).pages[State(owner).activePage]);const auto sourceRoot=State(owner).clientRoot;const auto source=client_notes::ReadNotebook(sourceRoot).notebook_snapshot;
  Check(snippet.links.size()==1,"Selected rich snippet includes its friendly URL");DestroyWindow(owner);
  Fixture(L"Persistence snippet target");owner=Open(L"Persistence snippet target");Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Support / ");Select(Edit(owner),0,7);Command(owner,IDC_NOTES_UNDERLINE);Select(Edit(owner),10,10);Save(owner);
  auto &state=State(owner);NotesTemplates library{owner,&state};library.snippets=true;
  Check(NotesInsertSnippet(owner,library,snippet),"Snippet insertion saves its native URL binding");
  Check(NotesNativeLinkAddress(Edit(owner),state,{0,7}).empty() && NotesNativeLinkAddress(Edit(owner),state,{10,17})==snippet.links.front().second,"Scoped insertion leaves equal underlined text outside the inserted range unlinked");
  Click(owner,10,17,snippet.links.front().second);Command(owner,IDC_NOTES_UNDO);
  Check(NotesWindowText(Edit(owner))==L"Support / " && NotesNativeLinkAddress(Edit(owner),state,{0,7}).empty(),"One Undo removes the entire snippet without linking unrelated text");
  Command(owner,IDC_NOTES_REDO);Save(owner);Click(owner,10,17,snippet.links.front().second);
  Check(client_notes::SameSnapshot(client_notes::ReadNotebook(sourceRoot).notebook_snapshot,source),"Cross-client snippet reuse leaves the source notebook byte-exact");
  CloseClientNotesDialog(owner,state);owner=Open(L"Persistence snippet target");Check(NotesNativeLinkAddress(Edit(owner),State(owner),{0,7}).empty(),"Reopening leaves the unrelated underlined caption unlinked");Click(owner,10,17,snippet.links.front().second);
  library.owner=owner;library.workspace=&State(owner);
  Select(Edit(owner),10,17);auto collision=snippet;collision.links.front().second=L"https://example.test/other";const auto text=NotesWindowText(Edit(owner));
  Check(NotesInsertSnippet(owner,library,collision) && NotesWindowText(Edit(owner))==text && NotesNativeLinkAddress(Edit(owner),State(owner),{0,7}).empty() && NotesNativeLinkAddress(Edit(owner),State(owner),{10,17})==collision.links.front().second,"Replacing a same-caption snippet binds only the selected occurrence to its own destination");DestroyWindow(owner);
}
static void ConflictCapture() {
  Fixture(L"Persistence capture failure");HWND owner=Open(L"Persistence capture failure");Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Original saved text");Save(owner);
  auto &state=State(owner);const HWND original=Edit(owner);Select(original,0,-1);Type(owner,L"Precious pending draft");auto external=state.original.pages;external.back().ticket=L"Other writer";
  Check(client_notes::WriteNotebook(state.clientRoot,external,state.original),"Competing notebook version is committed");const auto snapshot=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot;
  unsigned faults=2;SetWindowSubclass(original,FailCapture,77,reinterpret_cast<DWORD_PTR>(&faults));answers.push_back(IDYES);Command(owner,IDC_NOTES_CONFLICT);
  Check(faults==1 && Edit(owner)==original && IsWindow(original) && NotesWindowText(original)==L"Precious pending draft" && state.saveFailed,"Failed capture prevents destroying or replacing the uncaptured editor");
  Check(lastNotice.find(L"YES: Keep your changed notes")==std::wstring::npos && client_notes::SameSnapshot(client_notes::ReadNotebook(state.clientRoot).notebook_snapshot,snapshot),"Capture failure does not ask for destructive recovery or overwrite the external notebook");
  answers.clear();RemoveWindowSubclass(original,FailCapture,77);answers.push_back(IDYES);Command(owner,IDC_NOTES_CONFLICT);
  const auto saved=client_notes::ReadNotebook(state.clientRoot);
  Check(!state.saveFailed && NotesWindowText(Edit(owner))==L"Precious pending draft" && std::any_of(saved.pages.begin(),saved.pages.end(),[](const auto &p){return p.ticket==L"Other writer";}),"Retry recovers the complete draft alongside the external version");DestroyWindow(owner);
}
static void CommitSnapshots() {
  Fixture(L"Persistence quick snapshot");const auto root=g_sDataDir/L"Sites"/L"Persistence quick snapshot";
  NotesQuickState quick{L"Persistence quick snapshot",root,client_notes::ReadNotebook(root)};quick.pages=quick.baseline.pages;
  HWND window=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_QUICK),g_hGui,NotesQuickProc,reinterpret_cast<LPARAM>(&quick));SetDlgItemTextW(window,IDC_NOTES_QUICK_TEXT,L"Our capture");
  bool competed=false;afterNoteCommit=[&](const fs::path &path) {Check(path==root,"Quick commit race uses only its own client root");const auto latest=client_notes::ReadNotebook(root);auto pages=latest.pages;pages.front().ticket=L"Concurrent quick metadata";Check(client_notes::WriteNotebook(root,pages,latest),"Another writer commits between quick save and snapshot promotion");competed=true;};
  Check(NotesQuickSave(window,quick) && competed,"Quick save completes its own exact commit");const auto disk=client_notes::ReadNotebook(root).notebook_snapshot;
  SetDlgItemTextW(window,IDC_NOTES_QUICK_TEXT,L"Second pending capture");Check(!NotesQuickSave(window,quick) && NotesWindowText(GetDlgItem(window,IDC_NOTES_QUICK_TEXT))==L"Second pending capture" && client_notes::SameSnapshot(client_notes::ReadNotebook(root).notebook_snapshot,disk),"Quick save rejects the changed snapshot and keeps both writer data and pending text");DestroyWindow(window);
  Fixture(L"Persistence library snapshots");HWND owner=Open(L"Persistence library snapshots");
  for(bool snippets:{false,true}) {
    NotesTemplates library{owner,&State(owner)};library.snippets=snippets;Check(NotesLoadTemplates(library),"Reusable library snapshot fixture loads");auto pages=library.pages;pages.front().ticket=L"Our library change";competed=false;
    afterNoteCommit=[&](const fs::path &path) {Check(path==library.root,"Reusable commit race uses only its own library root");const auto latest=client_notes::ReadNotebook(path);auto newer=latest.pages;newer.back().ticket=L"Other reusable writer";Check(client_notes::WriteNotebook(path,newer,latest),"Another writer commits between library save and snapshot promotion");competed=true;};
    Check(NotesSaveTemplates(owner,library,pages) && competed,"Reusable save completes its own exact commit");const auto committed=client_notes::ReadNotebook(library.root).notebook_snapshot;
    pages=library.pages;pages.front().ticket=L"Our next pending edit";
    Check(!NotesSaveTemplates(owner,library,pages) && client_notes::SameSnapshot(client_notes::ReadNotebook(library.root).notebook_snapshot,committed),"Template/snippet retry protects a writer's newer content instead of adopting its snapshot");
  }
  DestroyWindow(owner);
}
static void ConflictDetails() {
  for(bool trash:{false,true}) {
    const auto name=trash ? L"Persistence trash Details" : L"Persistence open Details";Fixture(name);HWND owner=Open(name);Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Saved original");Save(owner);
    auto &state=State(owner);const auto id=state.attachedPageId;NotesShowPanel(owner,&state,true);SetDlgItemTextW(state.panel,IDC_NOTES_INPUT1,L"Our pending ticket");SetDlgItemTextW(state.panel,IDC_NOTES_INPUT2,L"invalid URL");CloseNotesPanel(state);
    Check(state.detailsDrafts.contains(id),"Invalid Details is keyed to its original note");
    Select(Edit(owner),0,-1);Type(owner,L"Our changed content");if(trash) Command(owner,IDC_NOTES_DELETE);
    auto external=state.original.pages;auto changed=std::find_if(external.begin(),external.end(),[&](const auto &p){return p.id==id;});changed->rtf=NotesPlainRtf(L"External changed content");changed->ticket=L"External ticket";changed->deleted=false;
    Check(client_notes::WriteNotebook(state.clientRoot,external,state.original),"External edit creates a Details recovery identity collision");answers.push_back(IDYES);Command(owner,IDC_NOTES_CONFLICT);
    uint64_t copy=0;for(const auto &[draftId,value]:state.detailsDrafts) if(draftId!=id) copy=draftId;
    const auto recovered=std::find_if(state.pages.begin(),state.pages.end(),[&](const auto &p){return p.id==copy;});
    Check(copy && !state.detailsDrafts.contains(id) && recovered!=state.pages.end() && recovered->deleted==trash,"Recovered invalid Details follows its own copy and preserves Trash membership");
    const auto original=std::find_if(state.pages.begin(),state.pages.end(),[&](const auto &p){return p.id==id;});Check(original!=state.pages.end() && original->ticket==L"External ticket","External writer's note receives none of the recovered invalid Details");
    NotesReviewInvalidDetails(owner,state);
    Check(trash ? openedLibraryFilter==3 && openedLibraryNote==copy : state.attachedPageId==copy && state.panel,"Review opens the recovered owner or matching Trash entry without silently restoring it");DestroyWindow(owner);
  }
}
static void LinkStylesAndReusableCaptions() {
  for(bool dark:{false,true}) {
    const bool priorDark=g_bThemeIsDark;const auto priorColors=g_themeColors;g_bThemeIsDark=dark;BuildThemeColors(dark,g_themeColors);UpdateThemeBrushes();
    const auto name=dark ? L"Persistence dark links" : L"Persistence light links";Fixture(name);HWND owner=Open(name);Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Support / Support");
    Link(owner,L"Support",L"https://example.test/style",0,7);Link(owner,L"Support",L"https://example.test/style",10,17);
    Select(Edit(owner),0,7);Command(owner,IDC_NOTES_LINK_REMOVE);Select(Edit(owner),10,17);Command(owner,IDC_NOTES_UNDERLINE);Save(owner);const auto id=State(owner).attachedPageId;
    Check(NotesHandoverHtml(State(owner),{State(owner).activePage}).find(L"https://example.test/style")!=std::wstring::npos,"Handover preserves a link whose underline style was removed");
    CloseClientNotesDialog(owner,State(owner));owner=Open(name);
    Check(State(owner).attachedPageId==id && NotesNativeLinkAddress(Edit(owner),State(owner),{0,7}).empty() && NotesNativeLinkAddress(Edit(owner),State(owner),{10,17})==L"https://example.test/style","Reopening preserves an underlineless link without resurrecting its explicitly unlinked duplicate");Click(owner,10,17,L"https://example.test/style");
    g_bThemeIsDark=!dark;BuildThemeColors(!dark,g_themeColors);UpdateThemeBrushes();RefreshNotesForTheme();Save(owner);
    Check(NotesNativeLinkAddress(Edit(owner),State(owner),{0,7}).empty() && NotesNativeLinkAddress(Edit(owner),State(owner),{10,17})==L"https://example.test/style","Theme refresh keeps only the actual linked duplicate bound");
    Select(Edit(owner),10,17);Command(owner,IDC_NOTES_HIGHLIGHT);Save(owner);CloseClientNotesDialog(owner,State(owner));owner=Open(name);
    Check(NotesNativeLinkAddress(Edit(owner),State(owner),{0,7}).empty() && NotesNativeLinkAddress(Edit(owner),State(owner),{10,17})==L"https://example.test/style","Highlighted underlineless links survive reopen without relinking a removed duplicate");
    Select(Edit(owner),10,17);Command(owner,IDC_NOTES_HIGHLIGHT);Command(owner,IDC_NOTES_UNDO);
    CHARFORMAT2W ink{sizeof(ink)};SendMessageW(Edit(owner),EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&ink));
    Check(!(ink.dwEffects&CFE_AUTOBACKCOLOR) && NotesNativeLinkAddress(Edit(owner),State(owner),{10,17})==L"https://example.test/style","One Undo restores a highlight action together with its correct link color");
    Select(Edit(owner),10,17);Command(owner,IDC_NOTES_CLEAR);Save(owner);CloseClientNotesDialog(owner,State(owner));owner=Open(name);
    Check(State(owner).pages[State(owner).activePage].links.empty() && NotesNativeLinkAddress(Edit(owner),State(owner),{10,17}).empty(),"Clear formatting and reopen do not resurrect removed URL metadata");DestroyWindow(owner);
    g_bThemeIsDark=priorDark;g_themeColors=priorColors;UpdateThemeBrushes();
  }
  Fixture(L"Persistence reusable captions");HWND owner=Open(L"Persistence reusable captions");Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Support");Link(owner,L"Support",L"https://example.test/reusable",0,7);
  for(bool snippets:{false,true}) {
    NotesTemplates library{owner,&State(owner)};library.snippets=snippets;library.startFromNote=true;Check(NotesLoadTemplates(library),"Editable reusable link fixture loads");
    HWND dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_TEMPLATE_EDITOR),owner,NotesTemplateEditorProc,reinterpret_cast<LPARAM>(&library));HWND edit=GetDlgItem(dialog,IDC_NOTES_PREVIEW);
    Select(edit,0,7);SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"Customer support"));
    Check(NotesTemplateEditorSave(dialog,library) && library.draft.links==std::vector<std::pair<std::wstring,std::wstring>>{{L"Customer support",L"https://example.test/reusable"}},"Editing a template/snippet caption captures the actual native URL label");
    SendMessageW(edit,EM_UNDO,0,0);Check(NotesTemplateEditorSave(dialog,library) && library.draft.links.front().first==L"Support","Reusable caption Undo restores its original native label and destination");
    SendMessageW(edit,EM_REDO,0,0);Check(NotesTemplateEditorSave(dialog,library) && library.draft.links.front().first==L"Customer support","Reusable caption Redo retains the updated label and original destination");
    const auto saved=library.draft;DestroyWindow(dialog);NotesTemplates reopened{owner,&State(owner)};reopened.snippets=snippets;Check(NotesLoadTemplates(reopened),"Edited reusable entry reopens from disk");
    const auto entry=std::find_if(reopened.pages.begin(),reopened.pages.end(),[&](const auto &p){return p.id==saved.id;});Check(entry!=reopened.pages.end() && entry->links==saved.links,"Reusable link metadata survives independent library reopening");
    if(snippets) {Select(Edit(owner),0,7);Check(NotesInsertSnippet(owner,reopened,*entry),"An edited linked snippet replaces the selected original caption");}
    else Check(NotesAddPage(owner,State(owner),*entry),"An edited linked template creates a note");
    Click(owner,0,16,L"https://example.test/reusable");
    if(!snippets) {Command(owner,IDC_NOTES_CLOSE_TAB);Check(NotesOpenSavedPage(owner,State(owner),1),"Return to source note after using the edited starter");}
  }
  DestroyWindow(owner);
}
static void DirectoryOwnership() {
  Fixture(L"Persistence lease Alpha");Fixture(L"Persistence lease Bravo");const auto alpha=g_sDataDir/L"Sites"/L"Persistence lease Alpha",bravo=g_sDataDir/L"Sites"/L"Persistence lease Bravo",aside=g_sDataDir/L"Sites"/L"Persistence preserved Alpha";
  HWND owner=Open(L"Persistence lease Alpha");Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Identical starting notebook");Save(owner);const auto expected=State(owner).original;DestroyWindow(owner);
  Check(client_notes::WriteNotebook(bravo,expected.pages,client_notes::ReadNotebook(bravo)),"Other client fixture has byte-identical notebook data");auto pending=expected.pages;pending.back().rtf=NotesPlainRtf(L"Alpha's private pending draft");
  Check(!client_notes::WriteNotebook(bravo,pending,expected) && client_notes::SameSnapshot(client_notes::ReadNotebook(bravo).notebook_snapshot,expected.notebook_snapshot),"Identical bytes and note IDs cannot authorize a different client directory");
  Check(MoveFileExW(alpha.c_str(),aside.c_str(),MOVEFILE_WRITE_THROUGH) && MoveFileExW(bravo.c_str(),alpha.c_str(),MOVEFILE_WRITE_THROUGH),"Replace pathname after snapshot with a different real client directory");
  Check(!client_notes::WriteNotebook(alpha,pending,expected) && client_notes::SameSnapshot(client_notes::ReadNotebook(alpha).notebook_snapshot,expected.notebook_snapshot),"A directory replaced before save is rejected by saved object identity");
  Check(MoveFileExW(alpha.c_str(),bravo.c_str(),MOVEFILE_WRITE_THROUGH) && MoveFileExW(aside.c_str(),alpha.c_str(),MOVEFILE_WRITE_THROUGH),"Restore independently owned fixture directories");
  unsigned calls=0;bool blocked=false;
  client_notes::DirectoryIdentity identity;{client_notes::NotebookDirectoryLease lease;Check(lease.Acquire(alpha),"Restored client root accepts an ordinary directory lease");identity=lease.identity;}
  std::string serialized;Check(identity==expected.directory_identity && client_notes::SerializeNotebook(pending,serialized),"Restored root identity and pending notebook pass save preflight");
  const bool rejected= !client_notes::WriteNotebook(alpha,pending,expected,[&] {++calls;blocked=MoveFileExW(alpha.c_str(),aside.c_str(),MOVEFILE_WRITE_THROUGH)==FALSE;return false;});
  std::cout<<"LEASE callback calls="<<calls<<", move blocked="<<blocked<<", save rejected="<<rejected<<std::endl;
  Check(rejected && calls==1 && blocked,"Client directory is pinned before the first save revalidation");
  calls=0;const auto sites=g_sDataDir/L"Sites",sitesAside=g_sDataDir/L"Replaced Sites";
  Check(!client_notes::WriteNotebook(alpha,pending,expected,[&] {++calls;blocked=MoveFileExW(sites.c_str(),sitesAside.c_str(),MOVEFILE_WRITE_THROUGH)==FALSE;return false;}) && blocked,"The real client ancestor cannot be substituted during save");
  const auto foreign=alpha/L"unrelated-foreign.tmp",foreignMoved=alpha/L"foreign-moved.tmp";Put(foreign,"FOREIGN BYTES");calls=0;bool sawStage=false,renameBlocked=false,replaceBlocked=false,deleteBlocked=false;
  Check(!client_notes::WriteNotebook(alpha,pending,expected,[&] {
    if(++calls!=2) return true;
    for(const auto &entry:fs::directory_iterator(alpha)) if(entry.path().filename().wstring().find(L".ctSpaces-notebook-")==0) {
      sawStage=true;renameBlocked=MoveFileExW(entry.path().c_str(),foreignMoved.c_str(),MOVEFILE_WRITE_THROUGH)==FALSE;
      replaceBlocked=MoveFileExW(foreign.c_str(),entry.path().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)==FALSE;
      deleteBlocked=DeleteFileW(entry.path().c_str())==FALSE;
    }
    return false;
  }) && sawStage && renameBlocked && replaceBlocked && deleteBlocked,"The staged file stays pinned through commit checks and cannot be renamed, replaced or deleted");
  Check(client_notes::SameSnapshot(client_notes::ReadNotebook(alpha).notebook_snapshot,expected.notebook_snapshot) && client_notes::ReadFileSnapshot(alpha,L"unrelated-foreign.tmp",64).bytes=="FOREIGN BYTES" && !fs::exists(foreignMoved),"Failed commit cleanup preserves the notebook and unrelated replacement object");
  bool leftover=false;for(const auto &entry:fs::directory_iterator(alpha)) leftover=leftover || entry.path().filename().wstring().find(L".ctSpaces-notebook-")==0;
  Check(!leftover && client_notes::WriteNotebook(alpha,pending,expected),"Exact staged handle is discarded on failure and a normal retry commits successfully");
  Check(client_notes::SameSnapshot(client_notes::ReadNotebook(bravo).notebook_snapshot,expected.notebook_snapshot),"Other client's notebook remains byte-exact through all save attacks");
  owner=Open(L"Persistence lease Alpha");auto &workspace=State(owner);const auto baseline=workspace.original;
  Check(client_notes::WriteNotebook(bravo,baseline.pages,client_notes::ReadNotebook(bravo)),"Native recovery race uses byte-identical client notebooks");Select(Edit(owner),0,-1);Type(owner,L"Precious Alpha draft still in its native editor");
  Check(MoveFileExW(alpha.c_str(),aside.c_str(),MOVEFILE_WRITE_THROUGH) && MoveFileExW(bravo.c_str(),alpha.c_str(),MOVEFILE_WRITE_THROUGH),"Substitute the owner pathname before the real native save command");
  Check(!SaveClientNotesDraft(owner,workspace,false) && NotesWindowText(Edit(owner))==L"Precious Alpha draft still in its native editor" && client_notes::SameSnapshot(client_notes::ReadNotebook(alpha).notebook_snapshot,baseline.notebook_snapshot) && !fs::exists(alpha/workspace.draftName),"Failed native save preserves the editor and writes no Alpha recovery draft into Bravo");
  Check(MoveFileExW(alpha.c_str(),bravo.c_str(),MOVEFILE_WRITE_THROUGH) && MoveFileExW(aside.c_str(),alpha.c_str(),MOVEFILE_WRITE_THROUGH),"Restore client pathnames after the native save attack");Save(owner);DestroyWindow(owner);
}
static void HistoryDestinations() {
  Fixture(L"Persistence history URLs");HWND owner=Open(L"Persistence history URLs");Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Support");Link(owner,L"Support",L"https://example.test/history-A",0,7);
  auto &state=State(owner);state.pages[state.activePage].history.clear();const auto linked=state.pages[state.activePage].rtf;
  Link(owner,L"Support",L"https://example.test/history-B",0,7);
  Check(!state.pages[state.activePage].history.empty() && state.pages[state.activePage].history.back().rtf==linked && state.pages[state.activePage].history.back().links.front().second==L"https://example.test/history-A","Destination-only change records the old URL even when native RTF is identical");
  auto restore=[&](size_t index) {NotesLibrary library{owner,&state};library.history=true;library.page=state.activePage;HWND window=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&library));
    const auto row=std::find(library.results.begin(),library.results.end(),index)-library.results.begin();SendDlgItemMessageW(window,IDC_NOTES_RESULTS,LB_SETCURSEL,row,0);Command(window,IDOK);DestroyWindow(window);};
  restore(0);Click(owner,0,7,L"https://example.test/history-A");Command(owner,IDC_NOTES_UNDO);Click(owner,0,7,L"https://example.test/history-B");Command(owner,IDC_NOTES_REDO);Save(owner);Click(owner,0,7,L"https://example.test/history-A");
  state.pages[state.activePage].history.clear();Select(Edit(owner),0,7);Command(owner,IDC_NOTES_LINK_REMOVE);Save(owner);
  Check(!state.pages[state.activePage].history.empty() && state.pages[state.activePage].history.front().links.front().second==L"https://example.test/history-A","Unlink stores the old document's URL in its real captured revision");
  CloseClientNotesDialog(owner,state);owner=Open(L"Persistence history URLs");auto &reopened=State(owner);
  Check(!reopened.pages[reopened.activePage].history.front().links.empty(),"Historical URLs survive a CTN v4 disk round trip");
  NotesLibrary library{owner,&reopened};library.history=true;library.page=reopened.activePage;HWND window=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&library));SendDlgItemMessageW(window,IDC_NOTES_RESULTS,LB_SETCURSEL,0,0);Command(window,IDOK);DestroyWindow(window);Click(owner,0,7,L"https://example.test/history-A");
  client_notes::Revision legacy{client_notes::Now(),linked};Check(NotesRestoreText(owner,reopened,legacy),"Legacy revision without saved URLs restores its text safely");Save(owner);
  Check(NotesNativeLinkAddress(Edit(owner),reopened,{0,7}).empty() && reopened.pages[reopened.activePage].links.empty(),"Legacy revision never substitutes a newer friendly-link destination");DestroyWindow(owner);
}
static void RevisionFormatSafety() {
  client_notes::NotePage page{L"History format",true,"{\\rtf1 Current}",{}};page.id=9;page.status=2;page.tags={L"handover"};
  page.history.push_back({client_notes::Now(),"{\\rtf1 Historical}",{{L"Support \u03a9",L"https://example.test/historical"}}});std::string bytes;std::vector<client_notes::NotePage> parsed;
  Check(client_notes::SerializeNotebook({page},bytes) && static_cast<unsigned char>(bytes[8])==4 && client_notes::ParseNotebook(bytes,parsed) && parsed.front().history.front().links==page.history.front().links && parsed.front().status==2 && parsed.front().tags==page.tags,"CTN v4 preserves revision Unicode URLs alongside work metadata");
  const auto good=bytes;auto older=good;older[8]=3;Check(!client_notes::ParseNotebook(older,parsed),"Revision URL payload cannot masquerade as an older notebook format");
  auto truncated=good;truncated.pop_back();Check(!client_notes::ParseNotebook(truncated,parsed),"Truncated v4 work metadata is rejected without partial pages");
  auto excessive=good;const auto revision=good.find(page.history.front().rtf);const auto count=revision+page.history.front().rtf.size();excessive[count]=1;excessive[count+1]=1;
  Check(revision!=std::string::npos && !client_notes::ParseNotebook(excessive,parsed),"A malformed revision URL count above 256 is rejected");
  auto invalid=good;const auto label=good.find("Support");invalid[label]=static_cast<char>(0xff);Check(label!=std::string::npos && !client_notes::ParseNotebook(invalid,parsed),"Invalid UTF-8 in historical link metadata is rejected");
  page.history.front().links.resize(257,{L"Label",L"https://example.test/link"});Check(!client_notes::SerializeNotebook({page},bytes),"Historical URL count remains bounded during serialization");
  page.history.front().links={{std::wstring(2049,L'x'),L"https://example.test/link"}};Check(!client_notes::SerializeNotebook({page},bytes),"Overlong historical link labels are rejected");
  page.history.front().links={{L"Label",std::wstring(2049,L'x')}};Check(!client_notes::SerializeNotebook({page},bytes),"Overlong historical link addresses are rejected");
  page.history.front().links.clear();Check(client_notes::SerializeNotebook({page},bytes) && static_cast<unsigned char>(bytes[8])==3 && client_notes::ParseNotebook(bytes,parsed),"Revision without URL data keeps the established CTN v3 format");
  page.status=0;page.tags.clear();Check(client_notes::SerializeNotebook({page},bytes) && static_cast<unsigned char>(bytes[8])==2 && client_notes::ParseNotebook(bytes,parsed) && parsed.front().history.front().links.empty(),"Legacy CTN v2 history remains readable with no invented destinations");
  std::string v1="CTNBOOK1";client_notes::AppendU32(v1,1);client_notes::AppendU32(v1,1);client_notes::AppendU32(v1,4);client_notes::AppendU32(v1,static_cast<uint32_t>(page.rtf.size()));v1+="Note";v1+=page.rtf;
  Check(client_notes::ParseNotebook(v1,parsed) && parsed.front().history.empty(),"CTN v1 notes remain readable without revision metadata");
  page.history.resize(21,page.history.front());Check(!client_notes::SerializeNotebook({page},bytes),"Revision count remains limited to twenty");
  page.history.resize(2);for(auto &item:page.history) item.rtf="{\\rtf1 "+std::string(16*1024*1024,'x')+"}";
  Check(!client_notes::SerializeNotebook({page},bytes),"Total revision size remains capped at 32 MiB");
}
static std::vector<CHARRANGE> CaptionRanges(HWND edit,const wchar_t *caption) {
  std::vector<CHARRANGE> ranges;FINDTEXTEXW find{{0,-1},caption,{}};
  while(SendMessageW(edit,EM_FINDTEXTEXW,FR_DOWN|FR_MATCHCASE,reinterpret_cast<LPARAM>(&find))>=0) {ranges.push_back(find.chrgText);find.chrg.cpMin=find.chrgText.cpMax;}return ranges;
}
static void LegacyRecoveryAndDraftCleanup() {
  for(const bool ambiguous:{false,true}) {
    const std::wstring name=ambiguous ? L"Persistence legacy ambiguous" : L"Persistence legacy invisible link";Fixture(name);
    const auto root=g_sDataDir/L"Sites"/name;client_notes::NotePage legacy{name,true,ambiguous ? "{\\rtf1\\ansi\\ul Support / Support\\ulnone}" : "{\\rtf1\\ansi Support}",{}};
    legacy.id=43;legacy.open=true;legacy.active=true;legacy.links={{L"Support",L"https://example.test/old-preserved"}};
    Check(client_notes::WriteNotebook(root,{legacy},client_notes::ReadNotebook(root)),"Legacy URL fixture writes its original safe notebook");const auto saved=client_notes::ReadNotebook(root).notebook_snapshot;
    HWND owner=Open(name);auto &state=State(owner);
    Check(state.editorLoadFailed && !state.attachedPageId && !HasActiveNote(state) && state.pages.front().links==legacy.links,"Unrestorable legacy metadata remains intact in an exportable window with no editable attachment");
    Check(lastNotice.find(L"Export client notebook")!=std::wstring::npos,"Legacy load warning explains exact export and safe closure");
    const auto exported=g_sDataDir/(ambiguous ? L"legacy-ambiguous-copy.ctn" : L"legacy-invisible-copy.ctn");files.push_back(exported.wstring());Command(owner,IDC_NOTES_EXPORT_ALL);
    Check(client_notes::ReadFileSnapshot(g_sDataDir,exported.filename().c_str(),client_notes::kMaxNotebookBytes).bytes==saved.bytes,"Existing notebook export preserves the raw legacy text and URL metadata exactly");
    Check(PrepareNotesForClientChange(name) && !IsWindow(owner) && client_notes::SameSnapshot(client_notes::ReadNotebook(root).notebook_snapshot,saved),"An unedited legacy load failure can close for client operations without rewriting its saved bytes");
    owner=Open(name);Fixture(L"Persistence safe legacy switch");const auto other=g_sDataDir/L"Sites"/L"Persistence safe legacy switch";
    Check(SwitchClientNotes(owner,State(owner),L"Persistence safe legacy switch",other) && State(owner).clientName==L"Persistence safe legacy switch" && client_notes::SameSnapshot(client_notes::ReadNotebook(root).notebook_snapshot,saved),"Switching away from an unedited legacy load failure preserves its original notebook");DestroyWindow(owner);
  }
  Fixture(L"Persistence owned draft cleanup");const auto root=g_sDataDir/L"Sites"/L"Persistence owned draft cleanup";
  client_notes::DirectoryIdentity draft;Check(client_notes::WriteAuxiliary(root,L"owned-draft.ctn","OUR DRAFT",&draft),"Crash draft records its exact file identity");const auto directory=client_notes::ReadNotebook(root).directory_identity;
  Check(MoveFileExW((root/L"owned-draft.ctn").c_str(),(root/L"saved-owned-draft.ctn").c_str(),MOVEFILE_WRITE_THROUGH),"Move the original draft away before substituting a foreign file");Put(root/L"owned-draft.ctn","FOREIGN DRAFT");
  Check(!client_notes::DeleteAuxiliary(root,L"owned-draft.ctn",directory,draft) && client_notes::ReadFileSnapshot(root,L"owned-draft.ctn",64).bytes=="FOREIGN DRAFT" && client_notes::ReadFileSnapshot(root,L"saved-owned-draft.ctn",64).bytes=="OUR DRAFT","Draft cleanup refuses a replacement object and preserves both original and foreign bytes");
  const auto recovery=root/L".ctSpaces-save-test-retained";fs::create_directory(recovery);Put(recovery/L"previous","RECOVERABLE COMPLETE VERSION");
  Check(client_notes::ReadFileSnapshot(root,L"missing-notebook.ctn",64).status==client_notes::ReadStatus::Error && client_notes::PersistenceFailure(L"Could not open").find(recovery.wstring())!=std::wstring::npos,"A missing document beside retained recovery data fails closed and exposes its exact folder path");
  fs::remove_all(recovery);client_notes::NotePage page{L"Saved",true,"{\\rtf1 Saved complete document}",{}};page.id=92;
  Check(client_notes::WriteNotebook(root,{page},client_notes::ReadNotebook(root)),"Write a complete current document before retained-recovery read validation");fs::create_directory(recovery);Put(recovery/L"previous","RECOVERABLE COMPLETE VERSION");
  Check(client_notes::ReadNotebook(root).status==client_notes::ReadStatus::Ok,"An authoritative complete CTN stays readable alongside retained versions despite missing legacy files");
}
static void OccurrenceBindings() {
  Fixture(L"Persistence Unicode occurrences");HWND owner=Open(L"Persistence Unicode occurrences");Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"\U0001f600\r\n\u03a9 Support | Support | Support");auto positions=CaptionRanges(Edit(owner),L"Support");Check(positions.size()==3,"Native caption search locates UTF-16 ranges after emoji and CRLF");
  Select(Edit(owner),positions[0].cpMin,positions[0].cpMax);Command(owner,IDC_NOTES_UNDERLINE);
  Link(owner,L"Support",L"https://example.test/occurrence-A",positions[1].cpMin,positions[1].cpMax);Link(owner,L"Support",L"https://example.test/occurrence-B",positions[2].cpMin,positions[2].cpMax);
  auto &page=State(owner).pages[State(owner).activePage];Check(page.linkRangesKnown && page.links.size()==2 && page.linkRanges.size()==2 && page.linkRanges[0].first==static_cast<uint32_t>(positions[1].cpMin),"Equal captions store distinct targets with precise native UTF-16 positions");
  State(owner).pages[State(owner).activePage].history.clear();Link(owner,L"Support",L"https://example.test/occurrence-C",positions[1].cpMin,positions[1].cpMax);
  Check(NotesNativeLinkAddress(Edit(owner),State(owner),positions[2])==L"https://example.test/occurrence-B","Editing one caption leaves the equal caption's independent destination intact");
  Command(owner,IDC_NOTES_UNDO);Click(owner,positions[1].cpMin,positions[1].cpMax,L"https://example.test/occurrence-A");Click(owner,positions[2].cpMin,positions[2].cpMax,L"https://example.test/occurrence-B");Command(owner,IDC_NOTES_REDO);Save(owner);
  auto snapshot=State(owner).pages[State(owner).activePage];Check(snapshot.history.front().linkRangesKnown,"History captures occurrence identity alongside the old targets");
  Select(Edit(owner),positions[0].cpMin,positions[2].cpMax);auto snippet=NotesSnippetFromSelection(owner,snapshot);Check(snippet.linkRangesKnown && snippet.linkRanges.size()==2 && snippet.linkRanges.front().first==static_cast<uint32_t>(positions[1].cpMin-positions[0].cpMin),"Selected snippet converts native occurrence positions into selection-relative offsets");
  Select(Edit(owner),positions[1].cpMin+1,positions[1].cpMax-1);auto partial=NotesSnippetFromSelection(owner,snapshot);Check(partial.links.size()==1 && partial.links.front().first==L"uppor" && partial.linkRanges.front().first==0 && partial.linkRanges.front().last==5,"Partial friendly-link selection captures its actual shortened caption and target");
  Select(Edit(owner),positions[1].cpMin,positions[1].cpMax);NotesCharacterFormat(owner,State(owner),CFM_UNDERLINE|CFM_COLOR,0,RGB(24,24,24));Save(owner);
  Check(NotesHandoverHtml(State(owner),{State(owner).activePage}).find(L"https://example.test/occurrence-C")!=std::wstring::npos && NotesDocumentLinks(State(owner).pages[State(owner).activePage]).size()==2,"Handover validates exact occurrences even without underline or link-colored ink");
  auto invalid=State(owner).pages[State(owner).activePage];invalid.name=L"Malformed range import";++invalid.linkRanges.front().first;const auto before=State(owner).original.notebook_snapshot;const auto count=State(owner).pages.size();
  Check(!NotesAddPage(owner,State(owner),invalid) && State(owner).pages.size()==count && client_notes::SameSnapshot(client_notes::ReadNotebook(State(owner).clientRoot).notebook_snapshot,before),"An imported range that does not match its native caption fails without replacing the draft or saved notebook");
  CloseClientNotesDialog(owner,State(owner));owner=Open(L"Persistence Unicode occurrences");positions=CaptionRanges(Edit(owner),L"Support");
  Check(NotesNativeLinkAddress(Edit(owner),State(owner),positions[0]).empty() && NotesNativeLinkAddress(Edit(owner),State(owner),positions[1])==L"https://example.test/occurrence-C" && NotesNativeLinkAddress(Edit(owner),State(owner),positions[2])==L"https://example.test/occurrence-B","Reopen keeps ordinary underline unlinked and same-caption URLs independent after Unicode/paragraph normalization");
  auto legacy=State(owner).pages[State(owner).activePage];legacy.name=L"Ambiguous legacy";legacy.linkRanges.clear();legacy.linkRangesKnown=false;legacy.links={{L"Support",L"https://example.test/legacy"}};const auto disk=State(owner).original.notebook_snapshot;
  Check(!NotesAddPage(owner,State(owner),legacy) && client_notes::SameSnapshot(client_notes::ReadNotebook(State(owner).clientRoot).notebook_snapshot,disk),"Ambiguous legacy caption metadata is preserved rather than assigned to ordinary underlines");DestroyWindow(owner);
  Fixture(L"Persistence Unicode insertion");owner=Open(L"Persistence Unicode insertion");Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"\U0001f47d destination\r\n");GETTEXTLENGTHEX measure{GTL_PRECISE|GTL_NUMCHARS,1200};const auto offset=SendMessageW(Edit(owner),EM_GETTEXTLENGTHEX,reinterpret_cast<WPARAM>(&measure),0);Select(Edit(owner),static_cast<LONG>(offset),static_cast<LONG>(offset));
  NotesTemplates library{owner,&State(owner)};library.snippets=true;Check(NotesInsertSnippet(owner,library,snippet),"Unicode-prefixed destination inserts the selected linked snippet");positions=CaptionRanges(Edit(owner),L"Support");Check(positions.size()==3 && NotesNativeLinkAddress(Edit(owner),State(owner),positions[0]).empty() && NotesNativeLinkAddress(Edit(owner),State(owner),positions[1])==L"https://example.test/occurrence-C" && NotesNativeLinkAddress(Edit(owner),State(owner),positions[2])==L"https://example.test/occurrence-B","Snippet offsets preserve each URL and the unrelated underline in a different client");
  Select(Edit(owner),0,0);Check(NotesInsertSnippet(owner,library,partial) && NotesNativeLinkAddress(Edit(owner),State(owner),{0,5})==L"https://example.test/occurrence-C","A partial link snippet inserts with its shortened caption and exact native target");CloseClientNotesDialog(owner,State(owner));owner=Open(L"Persistence Unicode insertion");positions=CaptionRanges(Edit(owner),L"Support");Check(positions.size()==3 && NotesNativeLinkAddress(Edit(owner),State(owner),positions[0]).empty() && NotesNativeLinkAddress(Edit(owner),State(owner),positions[2])==L"https://example.test/occurrence-B","Cross-client occurrence offsets survive independent disk reopening");DestroyWindow(owner);
  std::string bytes;std::vector<client_notes::NotePage> parsed;Check(client_notes::SerializeNotebook({snapshot},bytes) && static_cast<unsigned char>(bytes[8])==5 && client_notes::ParseNotebook(bytes,parsed) && parsed.front().linkRanges==snapshot.linkRanges,"CTN v5 round-trips exact occurrences and native positions");
  auto bad=snapshot;bad.linkRanges[0].last=bad.linkRanges[0].first;Check(!client_notes::SerializeNotebook({bad},bytes),"Zero-length persisted occurrence is rejected");bad=snapshot;bad.linkRanges[1].first=bad.linkRanges[0].first;Check(!client_notes::SerializeNotebook({bad},bytes),"Overlapping occurrence metadata is rejected");bad=snapshot;bad.linkRanges[0].target=256;Check(!client_notes::SerializeNotebook({bad},bytes),"Out-of-range destination indices are rejected");bad=snapshot;bad.linkRanges[0].last=client_notes::kMaxRichBytes+1;Check(!client_notes::SerializeNotebook({bad},bytes),"Out-of-bounds native character ranges are rejected");bad=snapshot;bad.links.push_back({L"Unused",L"https://example.test/unused"});Check(!client_notes::SerializeNotebook({bad},bytes),"Known occurrence metadata cannot silently orphan a stored target");
}
}
static void NotesPersistenceRound3() {
  notes_persistence::FixtureScope scope;notes_persistence::SnippetLinks();notes_persistence::ConflictCapture();notes_persistence::CommitSnapshots();notes_persistence::ConflictDetails();notes_persistence::LinkStylesAndReusableCaptions();notes_persistence::DirectoryOwnership();notes_persistence::HistoryDestinations();notes_persistence::RevisionFormatSafety();notes_persistence::OccurrenceBindings();notes_persistence::LegacyRecoveryAndDraftCleanup();
}
