static void RichSnapshotAndContexts(const fs::path &artifacts) {
  ++groups;Fixture(L"Rich Markdown Alpha");HWND owner=Open(L"Rich Markdown Alpha");auto &state=State(owner);
  client_notes::NotePage other;other.name=L"Other note";other.rich=true;other.rtf=NotesPlainRtf(L"Other content");
  Check(NotesAddPage(owner,state,std::move(other)),"Context fixture owns an independent second note");
  const auto parsed=notes_markdown::Parse(L"# Rich title\nA **bold** and *italic* [portal](https://example.test/rich).\n- [x] Completed\n- [ ] Pending\n");
  const auto rendered=notes_markdown::Render(parsed.document);client_notes::NotePage page;std::wstring reason;
  Check(NotesMarkdownPage(rendered,L"Rich interchange",page,reason),"A rich Markdown fixture preserves native link positions");
  Check(NotesAddPage(owner,state,std::move(page)),"A rich Markdown fixture enters the real client notebook");
  HWND edit=Edit(owner);const auto noteId=state.pages[state.activePage].id;
  Select(edit,2,5);CHARRANGE before{},after{};NotesSelection(edit,before);POINT scrollBefore{},scrollAfter{};SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scrollBefore));
  const auto bytes=client_notes::ReadNotebook(g_sDataDir/L"Sites/Rich Markdown Alpha").notebook_snapshot.bytes;
  notes_markdown::Document snapshot;std::wstring markdown;
  Check(NotesMarkdownSnapshot(edit,state.pages[state.activePage],snapshot,reason),"Read-only native TOM snapshot recognizes supported rich Markdown content");
  Check(notes_markdown::Write(snapshot,markdown,reason) && markdown.find(L"# Rich title")!=std::wstring::npos && markdown.find(L"**bold**")!=std::wstring::npos && markdown.find(L"https")!=std::wstring::npos,
        "Native rich export keeps headings, emphasis and inline web destinations");
  Check(notes_markdown::Canonical(snapshot)==notes_markdown::Canonical(parsed.document),"Native rich Markdown round-trip preserves representable block and span semantics");
  NotesSelection(edit,after);SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scrollAfter));
  Check(before.cpMin==after.cpMin && before.cpMax==after.cpMax && scrollBefore.x==scrollAfter.x && scrollBefore.y==scrollAfter.y &&
        client_notes::ReadNotebook(g_sDataDir/L"Sites/Rich Markdown Alpha").notebook_snapshot.bytes==bytes,
        "Read-only rich export neither moves the native selection/scroll nor writes the notebook");
  const auto intended=[&](){return static_cast<size_t>(std::find_if(state.pages.begin(),state.pages.end(),[&](const auto &entry){return entry.id==noteId;})-state.pages.begin());};
  const auto alternate=[&](){return static_cast<size_t>(std::find_if(state.pages.begin(),state.pages.end(),[&](const auto &entry){return entry.id!=noteId;})-state.pages.begin());};
  const fs::path source=artifacts/L"stale-import.md";Put(source,"Never import into another note\n");
  const auto count=state.pages.size();
  struct RestoreObservers {
    decltype(notesFilePickerObserver) picker=std::move(notesFilePickerObserver);
    decltype(notesDialogObserver) dialog=std::move(notesDialogObserver);
    ~RestoreObservers(){notesFilePickerObserver=std::move(picker);notesDialogObserver=std::move(dialog);}
  } restore;
  notesFilePickerObserver=[&](OPENFILENAMEW *picker)->BOOL {
    if(!NotesOpenSavedPage(owner,state,alternate()))throw std::runtime_error("Stale picker fixture could not switch note");
    wcscpy_s(picker->lpstrFile,picker->nMaxFile,source.c_str());return TRUE;
  };
  Check(!NotesImportMarkdown(owner,state) && state.pages.size()==count && lastNotice.find(L"changed")!=std::wstring::npos,
        "A page switch during the import picker refuses stale-context creation");
  NotesOpenSavedPage(owner,state,intended());
  const fs::path staleOutput=artifacts/L"stale-export.md";
  notesFilePickerObserver=[&](OPENFILENAMEW *picker)->BOOL {
    if(!NotesOpenSavedPage(owner,state,alternate()))throw std::runtime_error("Stale export fixture could not switch note");
    wcscpy_s(picker->lpstrFile,picker->nMaxFile,staleOutput.c_str());return TRUE;
  };
  answers.push_back(IDOK);
  Check(!NotesExportMarkdown(owner,state) && !fs::exists(staleOutput),"A page switch during the export picker cannot write a stale-context file");
  NotesOpenSavedPage(owner,state,intended());notesFilePickerObserver={};
  notesDialogObserver=[&](LPCWSTR resource,DLGPROC procedure,LPARAM data)->INT_PTR {
    if(resource!=MAKEINTRESOURCEW(IDD_NOTES_INPUT) || procedure!=NotesInputProc)throw std::runtime_error("Unexpected table context dialog");
    auto &input=*reinterpret_cast<NotesInput *>(data);input.first=L"3";input.second=L"3";
    if(!NotesOpenSavedPage(owner,state,alternate()))throw std::runtime_error("Stale table fixture could not switch note");return IDOK;
  };
  Check(!NotesCreateSimpleTable(owner,state) && state.pages.size()==count && state.pages[state.activePage].rtf.find("\\trowd")==std::string::npos,
        "A page switch during table dimensions cannot insert into the replacement note");
  DestroyWindow(owner);NotesBackgroundAudit("rich Markdown read-only snapshot and pumped-dialog note identity guards");
}
