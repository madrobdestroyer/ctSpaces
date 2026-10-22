// Persistence is checked through a separately parsed notebook and a fresh native
// decoder, rather than through the workspace's latest in-memory page vector.
struct MarkdownDurableView {
  bool ok=false;
  std::wstring raw;
  std::vector<std::wstring> cells;
  std::string rtf;
};
static MarkdownDurableView MarkdownNativeView(HWND edit) {
  MarkdownDurableView view;
  Microsoft::WRL::ComPtr<IUnknown> unknown;
  Microsoft::WRL::ComPtr<ITextDocument> document;
  Microsoft::WRL::ComPtr<ITextRange> range;
  SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(unknown.GetAddressOf()));
  LONG story=0;
  if(!unknown || FAILED(unknown.As(&document)) || FAILED(document->Range(0,0,&range)) ||
      FAILED(range->GetStoryLength(&story)) || story<1 || FAILED(range->SetRange(0,story-1)))return view;
  BSTR raw=nullptr;if(FAILED(range->GetText(&raw)))return view;
  view.raw.assign(raw?raw:L"",raw?SysStringLen(raw):0);SysFreeString(raw);
  for(size_t at=0;at<view.raw.size();++at)if(view.raw[at]==L'\a') {
    LONG moved=0,first=-1;
    if(FAILED(range->SetRange(static_cast<LONG>(at),static_cast<LONG>(at))) ||
        FAILED(range->StartOf(tomCell,tomMove,&moved)) || FAILED(range->GetStart(&first)) ||
        first<0 || first>static_cast<LONG>(at))return view;
    std::wstring text=view.raw.substr(static_cast<size_t>(first),at-static_cast<size_t>(first));
    // A final native paragraph terminator is structural; soft source breaks
    // remain exact vertical-tab characters in this independent cell oracle.
    if(!text.empty() && text.back()==L'\r')text.pop_back();
    view.cells.push_back(std::move(text));
  }
  view.ok=StreamClientNotesOut(edit,view.rtf);return view;
}
static MarkdownDurableView MarkdownDecodeSaved(HWND owner,const client_notes::NotePage &page) {
  HWND decoder=CreateWindowExW(0,L"RICHEDIT50W",L"",WS_CHILD|ES_MULTILINE,0,0,1,1,owner,nullptr,g_hInst,nullptr);
  if(!decoder)return {};
  SendMessageW(decoder,EM_EXLIMITTEXT,0,client_notes::kMaxRichBytes);
  NotesStream stream{page.rtf};EDITSTREAM input{reinterpret_cast<DWORD_PTR>(&stream),0,NotesStreamIn};
  SendMessageW(decoder,EM_STREAMIN,SF_RTF,reinterpret_cast<LPARAM>(&input));
  auto view=MarkdownNativeView(decoder);
  view.ok=view.ok && !input.dwError && stream.offset==stream.bytes.size();
  DestroyWindow(decoder);return view;
}
static bool MarkdownSameView(const MarkdownDurableView &left,const MarkdownDurableView &right) {
  return left.ok && right.ok && left.raw==right.raw && left.cells==right.cells;
}
static bool MarkdownAwaitAutosave(HWND owner) {
  const auto deadline=GetTickCount64()+1600;
  while(GetTickCount64()<deadline) {
    MSG event{};
    while(PeekMessageW(&event,owner,WM_TIMER,WM_TIMER,PM_REMOVE)) {
      const bool saved=event.wParam==1;DispatchMessageW(&event);
      if(saved)return true;
    }
    MsgWaitForMultipleObjectsEx(0,nullptr,25,QS_TIMER,MWMO_INPUTAVAILABLE);
  }
  return false;
}
static bool MarkdownSavedWork(HWND owner,const fs::path &root,const MarkdownDurableView &expected,
    const std::wstring &ticket,const std::wstring &url) {
  const auto saved=client_notes::ReadNotebook(root);
  const auto page=std::find_if(saved.pages.begin(),saved.pages.end(),[](const auto &item){return item.id==11;});
  if(saved.status!=client_notes::ReadStatus::Ok || saved.pages.size()!=2 || page==saved.pages.end())return false;
  const bool metadata=page->name==L"Work" && page->ticket==ticket && page->ticketUrl==url+L"/ticket" &&
      page->status==2 && page->tags==std::vector<std::wstring>{L"keep",L"client-bound"} &&
      page->links==std::vector<std::pair<std::wstring,std::wstring>>{{L"Portal",url}} &&
      page->linkRangesKnown && page->linkRanges.size()==1 && page->linkRanges[0].first==0 &&
      page->linkRanges[0].last==6 && page->linkRanges[0].target==0;
  const auto actual=MarkdownDecodeSaved(owner,*page);
  const bool same=metadata && page->rtf==expected.rtf && MarkdownSameView(actual,expected);
  if(!same) {
    std::cout<<"Markdown durable mismatch: metadata="<<metadata<<" exactStoredRtf="<<(page->rtf==expected.rtf)
      <<" decoded="<<actual.ok<<" raw="<<(actual.raw==expected.raw)<<" cells="<<(actual.cells==expected.cells)
      <<" expectedRtfBytes="<<expected.rtf.size()<<" storedRtfBytes="<<page->rtf.size()<<" cells="<<actual.cells.size()<<"/"<<expected.cells.size()<<"\n";
    Put(g_sDataDir/L"markdown-failure-expected.rtf",expected.rtf);Put(g_sDataDir/L"markdown-failure-stored.rtf",page->rtf);
    const auto live=MarkdownNativeView(Edit(owner));Put(g_sDataDir/L"markdown-failure-live.rtf",live.rtf);
    std::string encoded;client_notes::EncodeUtf8(actual.raw,encoded);Put(g_sDataDir/L"markdown-failure-stored-raw.txt",encoded);
    client_notes::EncodeUtf8(expected.raw,encoded);Put(g_sDataDir/L"markdown-failure-expected-raw.txt",encoded);
    std::wcout<<L"Markdown durable diagnostic directory: "<<g_sDataDir.wstring()<<L"\n";
  }
  return same;
}
static void MarkdownAutosaveUndoIsolation() {
  ++groups;
  for(const bool code:{false,true}) {
    const std::wstring prefix=code?L"Code autosave ":L"Table autosave ";
    const std::wstring alpha=prefix+L"Alpha",beta=prefix+L"Beta";
    Fixture(alpha);Fixture(beta);
    const auto alphaRoot=g_sDataDir/L"Sites"/alpha,betaRoot=g_sDataDir/L"Sites"/beta;
    const std::wstring alphaUrl=L"https://example.test/alpha-"+(code?std::wstring(L"code"):std::wstring(L"table"));
    const std::wstring betaUrl=L"https://example.test/beta-"+(code?std::wstring(L"code"):std::wstring(L"table"));
    const auto seed=[&](const fs::path &root,const std::wstring &ticket,const std::wstring &url,const std::wstring &body) {
      client_notes::NotePage work;work.id=11;work.name=L"Work";work.rich=true;work.rtf=NotesPlainRtf(L"Portal\r\n"+body+L"\r\n");
      work.created=work.modified=client_notes::Now();work.active=true;
      work.ticket=ticket;work.ticketUrl=url+L"/ticket";work.status=2;work.tags={L"keep",L"client-bound"};
      work.links={{L"Portal",url}};work.linkRangesKnown=true;work.linkRanges={{0,6,0}};
      auto sibling=work;sibling.id=22;sibling.name=L"Sibling";sibling.active=false;sibling.rtf=NotesPlainRtf(L"Independent sibling "+body);
      sibling.links.clear();sibling.linkRanges.clear();sibling.linkRangesKnown=false;
      Check(client_notes::WriteNotebook(root,{work,sibling},client_notes::ReadNotebook(root)),
            "Two-client autosave fixture commits independently owned notes with colliding local IDs");
    };
    seed(alphaRoot,L"T20261020.0011",alphaUrl,L"Alpha body \U0001f680");
    seed(betaRoot,L"T20261020.0022",betaUrl,L"Beta body \u4e2d\u6587");
    HWND owner=Open(alpha);auto &state=State(owner);
    Check(NotesOpenSavedPage(owner,state,1) && NotesOpenSavedPage(owner,state,0),
          "Both source notes complete their one-time native normalization before durable invariants are recorded");
    HWND originalEdit=Edit(owner);
    const auto before=MarkdownNativeView(originalEdit);
    const auto betaBytes=client_notes::ReadNotebook(betaRoot).notebook_snapshot.bytes;
    const auto siblingBefore=client_notes::ReadNotebook(alphaRoot).pages[1].rtf;
    Check(before.ok && state.attachedPageId==11 && NotesNativeLinkAddress(originalEdit,state,{0,6})==alphaUrl,
          "The source starts with its own exact native URL despite colliding other-client IDs");
    Select(originalEdit,-1,-1);
    if(code) {
      NotesCodeInput input;input.language=1;input.source=L"Get-Printer\r\nWrite-Output \"\u4e2d \U0001f680\"";
      Check(NotesInsertCode(owner,state,input),"Code insertion succeeds through the real autosaving native command");
    } else Check(NotesInsertSimpleTable(owner,state,2,2),"Table insertion succeeds through the real autosaving native command");
    const auto inserted=MarkdownNativeView(Edit(owner));
    Check(inserted.ok && (code?inserted.cells==std::vector<std::wstring>{L"PowerShell - Code",L"Get-Printer\vWrite-Output \"\u4e2d \U0001f680\""}:
          inserted.cells==std::vector<std::wstring>{L"",L"",L"",L""}),
          "Independent native story and cell markers expose every inserted table/code cell exactly");
    Check(MarkdownSavedWork(owner,alphaRoot,inserted,L"T20261020.0011",alphaUrl),
          "Insertion autosave preserves exact RTF, raw cells, stored URL ranges and ticket metadata");
    const auto autoSave=[&](const MarkdownDurableView &expected,const char *description) {
      // Await and dispatch the real debounce timer. Calling Save() or posting a
      // fabricated timer would conceal a missing Undo/Redo change notification.
      const bool timer=MarkdownAwaitAutosave(owner),durable=MarkdownSavedWork(owner,alphaRoot,expected,L"T20261020.0011",alphaUrl);
      const bool isolated=client_notes::ReadNotebook(betaRoot).notebook_snapshot.bytes==betaBytes;
      if(!timer || state.saveFailed || !durable || !isolated)std::cout<<"Markdown autosave diagnostic: timer="<<timer
        <<" saveFailed="<<state.saveFailed<<" durable="<<durable<<" isolated="<<isolated<<" modified="<<SendMessageW(Edit(owner),EM_GETMODIFY,0,0)
        <<" formattingChanged="<<state.formattingChanged<<" metadataChanged="<<state.metadataChanged<<"\n";
      Check(timer && !state.saveFailed && durable && isolated,description);
    };
    Command(owner,IDC_NOTES_UNDO);
    Check(MarkdownSameView(MarkdownNativeView(Edit(owner)),before),"One real Undo removes the complete inserted table/code without changing the source prose");
    autoSave(before,"Autosave after insertion Undo writes exact original RTF/text/links and leaves the other client untouched");
    Check(NotesOpenSavedPage(owner,state,1) && state.attachedPageId==22 && NotesOpenSavedPage(owner,state,0) &&
          Edit(owner)==originalEdit && SendMessageW(originalEdit,EM_CANREDO,0,0),
          "A same-client sibling switch preserves the source editor and its Redo after Undo autosave");
    Command(owner,IDC_NOTES_REDO);
    Check(MarkdownSameView(MarkdownNativeView(Edit(owner)),inserted),"Redo restores every table/code cell after saving and switching to the sibling note");
    autoSave(inserted,"Autosave after insertion Redo restores exact inserted RTF and client-bound metadata");
    const auto crossClient=[&](const MarkdownDurableView &expected,const char *description) {
      const auto alphaBytes=client_notes::ReadNotebook(alphaRoot).notebook_snapshot.bytes;
      Check(SwitchClientNotes(owner,state,beta,betaRoot) && state.attachedPageId==11 &&
            NotesNativeLinkAddress(Edit(owner),state,{0,6})==betaUrl &&
            MarkdownNativeView(Edit(owner)).raw.find(L"Beta body \u4e2d\u6587")!=std::wstring::npos &&
            state.pages[state.activePage].ticket==L"T20261020.0022" &&
            client_notes::ReadNotebook(alphaRoot).notebook_snapshot.bytes==alphaBytes,
            "Switching to a colliding note ID loads only the independent other-client text, URL and ticket");
      Check(SwitchClientNotes(owner,state,alpha,alphaRoot) && state.attachedPageId==11 &&
            MarkdownSameView(MarkdownNativeView(Edit(owner)),expected) &&
            NotesNativeLinkAddress(Edit(owner),state,{0,6})==alphaUrl &&
            MarkdownSavedWork(owner,alphaRoot,expected,L"T20261020.0011",alphaUrl) &&
            client_notes::ReadNotebook(betaRoot).notebook_snapshot.bytes==betaBytes,description);
    };
    crossClient(inserted,"Client switch/reload independently restores saved table/code RTF, every cell and original source URL");
    const auto reloaded=MarkdownNativeView(Edit(owner));
    // RichEdit can remove a redundant final \cf0 token while loading. The
    // before2 proof preserved both exact streams and showed that sole change.
    // Cell Undo must restore this fresh editor's own exact pre-edit stream;
    // the insertion phase above still checks its original exact durable RTF.
    std::cout<<"Markdown reload diagnostic: exactRTF="<<(reloaded.rtf==inserted.rtf)<<" raw="<<(reloaded.raw==inserted.raw)
      <<" cells="<<(reloaded.cells==inserted.cells)<<" beforeRtfBytes="<<inserted.rtf.size()<<" reloadedRtfBytes="<<reloaded.rtf.size()<<"\n";
    Put(g_sDataDir/L"markdown-before-client-reload.rtf",inserted.rtf);Put(g_sDataDir/L"markdown-after-client-reload.rtf",reloaded.rtf);
    {
      Microsoft::WRL::ComPtr<IUnknown> unknown;Microsoft::WRL::ComPtr<ITextDocument> document;
      SendMessageW(Edit(owner),EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(unknown.GetAddressOf()));
      const LONG at=unknown && SUCCEEDED(unknown.As(&document))?NativeTableCellStart(document.Get(),code?1u:0u):-1;
      Check(at>=0,"Reopened source exposes its intended editable code/table cell independently of the navigation helper");
      Select(Edit(owner),at,at);
    }
    SendMessageW(Edit(owner),EM_STOPGROUPTYPING,0,0);Type(owner,L"Edited \u03a9 ");SendMessageW(Edit(owner),EM_STOPGROUPTYPING,0,0);
    const auto edited=MarkdownNativeView(Edit(owner));
    auto wantedCells=inserted.cells;wantedCells[code?1:0]=L"Edited \u03a9 "+wantedCells[code?1:0];
    Check(edited.ok && edited.cells==wantedCells && edited.raw!=inserted.raw,"Cell editing changes only the intended cell's exact Unicode content");
    autoSave(edited,"Autosave persists a real code/table cell edit with its exact RTF and unchanged external links");
    Command(owner,IDC_NOTES_UNDO);
    Check(MarkdownSameView(MarkdownNativeView(Edit(owner)),reloaded),"Cell-edit Undo restores all native cell boundaries and original source text");
    autoSave(reloaded,"Autosave persists cell-edit Undo instead of silently retaining the later cell contents");
    Command(owner,IDC_NOTES_REDO);
    Check(MarkdownSameView(MarkdownNativeView(Edit(owner)),edited),"Cell-edit Redo restores its complete Unicode text without altering another cell");
    autoSave(edited,"Autosave persists cell-edit Redo rather than retaining the older notebook snapshot");
    Command(owner,IDC_NOTES_UNDO);
    autoSave(reloaded,"A second cell-edit Undo is durable before another client switch");
    crossClient(reloaded,"Undo autosave survives a colliding-ID client round trip and fresh native editor reconstruction");
    CloseClientNotesDialog(owner,state);Check(!IsWindow(owner),"The tested source closes through the real save-on-close lifecycle");
    owner=Open(alpha);auto &reopened=State(owner);
    Check(reopened.attachedPageId==11 && MarkdownSameView(MarkdownNativeView(Edit(owner)),reloaded) &&
          NotesNativeLinkAddress(Edit(owner),reopened,{0,6})==alphaUrl &&
          MarkdownSavedWork(owner,alphaRoot,reloaded,L"T20261020.0011",alphaUrl),
          "A completely reopened workspace restores exact table/code cells, RTF, URL mapping and metadata after Undo autosave");
    const auto finalAlpha=client_notes::ReadNotebook(alphaRoot);
    Check(finalAlpha.pages.size()==2 && finalAlpha.pages[1].rtf==siblingBefore &&
          client_notes::ReadNotebook(betaRoot).notebook_snapshot.bytes==betaBytes,
          "All Undo/Redo/autosave/switch/reopen cycles preserve the untouched sibling and exact other-client durable bytes");
    CloseClientNotesDialog(owner,reopened);
    NotesBackgroundAudit(code?"code autosave after Undo/Redo, colliding client IDs and full reopen":"table autosave after Undo/Redo, colliding client IDs and full reopen");
  }
}
