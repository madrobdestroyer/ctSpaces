// Fault recovery only: inject a late read failure into an actual attached
// editor stream, or exhaust cookie IDs immediately before an actual link edit.
// Preview decoders, the filesystem and other clients are never fault targets.
namespace notes_rollback {
struct View {bool ok=false;std::string rtf;std::wstring raw;};
static View Read(HWND edit) {
  View view;IUnknown *unknown=nullptr;ITextDocument *document=nullptr;ITextRange *range=nullptr;
  SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(&unknown));
  if(unknown){unknown->QueryInterface(__uuidof(ITextDocument),reinterpret_cast<void **>(&document));unknown->Release();}
  LONG story=0;
  if(document && SUCCEEDED(document->Range(0,0,&range)) && SUCCEEDED(range->GetStoryLength(&story)) && story>0 &&
      SUCCEEDED(range->SetRange(0,story-1))) {
    BSTR text=nullptr;if(SUCCEEDED(range->GetText(&text))) {
      view.raw.assign(text?text:L"",text?SysStringLen(text):0);SysFreeString(text);
      view.ok=StreamClientNotesOut(edit,view.rtf);
    }
  }
  if(range)range->Release();if(document)document->Release();return view;
}
static bool Same(const View &a,const View &b){return a.ok && b.ok && a.raw==b.raw && a.rtf==b.rtf;}
static bool AwaitTimer(HWND owner) {
  const auto deadline=GetTickCount64()+1600;
  while(GetTickCount64()<deadline) {
    MSG event{};while(PeekMessageW(&event,owner,WM_TIMER,WM_TIMER,PM_REMOVE)) {
      const bool autosave=event.wParam==1;DispatchMessageW(&event);if(autosave)return true;
    }
    MsgWaitForMultipleObjectsEx(0,nullptr,25,QS_TIMER,MWMO_INPUTAVAILABLE);
  }
  return false;
}
struct LateStreamFailure {
  bool armed=true,observed=false,mutated=false,linkOnly=false;
  size_t limit=0,delivered=0,total=0;
  DWORD error=0;EDITSTREAM *original=nullptr;
  View before,partial;
};
static DWORD CALLBACK PartialRead(DWORD_PTR cookie,LPBYTE bytes,LONG wanted,LONG *actual) {
  auto &fault=*reinterpret_cast<LateStreamFailure *>(cookie);
  if(fault.delivered>=fault.limit){*actual=0;return ERROR_READ_FAULT;}
  const LONG bounded=static_cast<LONG>((std::min)(static_cast<size_t>((std::max)(0L,wanted)),fault.limit-fault.delivered));
  const DWORD error=fault.original->pfnCallback(fault.original->dwCookie,bytes,bounded,actual);
  if(*actual>0)fault.delivered+=static_cast<size_t>(*actual);
  return error;
}
static LRESULT CALLBACK FailActualStream(HWND edit,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
  auto &fault=*reinterpret_cast<LateStreamFailure *>(data);
  if(message==WM_NCDESTROY)RemoveWindowSubclass(edit,FailActualStream,id);
  if(message==EM_REPLACESEL && fault.armed && fault.linkOnly) {
    fault.armed=false;fault.observed=true;
    const auto result=DefSubclassProc(edit,message,w,l);
    fault.partial=Read(edit);fault.mutated=fault.partial.ok && !Same(fault.before,fault.partial);
    return result;
  }
  if(message==EM_STREAMIN && (w&SFF_SELECTION) && fault.armed && !fault.linkOnly) {
    fault.armed=false;fault.observed=true;fault.original=reinterpret_cast<EDITSTREAM *>(l);
    const auto &source=*reinterpret_cast<NotesStream *>(fault.original->dwCookie);
    // The real callback reads all but the final closing byte, then returns an
    // actual read error. The native control has already processed the source.
    fault.total=source.bytes.size();fault.limit=fault.total?fault.total-1:0;
    EDITSTREAM partial{reinterpret_cast<DWORD_PTR>(&fault),0,PartialRead};
    const auto result=DefSubclassProc(edit,message,w,reinterpret_cast<LPARAM>(&partial));
    fault.original->dwError=partial.dwError;fault.error=partial.dwError;
    fault.partial=Read(edit);fault.mutated=fault.partial.ok && !Same(fault.before,fault.partial);
    return result;
  }
  return DefSubclassProc(edit,message,w,l);
}
static bool Durable(HWND owner,const fs::path &root,const View &expected,const std::wstring &url) {
  const auto saved=client_notes::ReadNotebook(root);
  if(saved.status!=client_notes::ReadStatus::Ok || saved.pages.size()!=1)return false;
  const auto &page=saved.pages.front();
  if(page.id!=11 || page.rtf!=expected.rtf || page.ticket!=L"T20261020.0111" || page.status!=2 ||
      page.tags!=std::vector<std::wstring>{L"rollback",L"keep"} ||
      page.links!=std::vector<std::pair<std::wstring,std::wstring>>{{L"Portal",url}} ||
      !page.linkRangesKnown || page.linkRanges.size()!=1 || page.linkRanges[0].first!=0 ||
      page.linkRanges[0].last!=6 || page.linkRanges[0].target!=0)return false;
  HWND decoder=CreateWindowExW(0,L"RICHEDIT50W",L"",WS_CHILD|ES_MULTILINE,0,0,1,1,owner,nullptr,g_hInst,nullptr);
  if(!decoder)return false;
  NotesStream stream{page.rtf};EDITSTREAM input{reinterpret_cast<DWORD_PTR>(&stream),0,NotesStreamIn};
  SendMessageW(decoder,EM_STREAMIN,SF_RTF,reinterpret_cast<LPARAM>(&input));const auto actual=Read(decoder);
  DestroyWindow(decoder);return !input.dwError && stream.offset==stream.bytes.size() && actual.ok && actual.raw==expected.raw;
}
static unsigned failures=0;
static std::string diagnosticCase;
static void Expect(bool condition,const std::string &label) {
  ++checks;if(!condition)++failures;
  std::cout<<(condition?"PASS ":"FAIL ")<<label<<std::endl;
}
struct Scope {
  fs::path previousData=g_sDataDir,previousConfig=g_sConfigPath;
  Scope(){g_sDataDir/=L"RollbackTransactions";fs::create_directories(g_sDataDir);g_sConfigPath=g_sDataDir/L"ctSpaces.ini";}
  ~Scope(){notes_undo::diagnosticSink={};g_notesAppUndoObserver=nullptr;g_notesAppUndoContext=nullptr;inputs.clear();if(g_hNotesWindow && IsWindow(g_hNotesWindow))DestroyWindow(g_hNotesWindow);g_sDataDir=previousData;g_sConfigPath=previousConfig;}
};
static void Run(unsigned kind,const fs::path &artifacts) {
  const char *labels[]={"restore","snippet","table","link"};const std::string label=labels[kind];
  diagnosticCase=label;
  const std::wstring alpha=L"Rollback "+std::wstring(label.begin(),label.end())+L" Alpha",beta=L"Rollback "+std::wstring(label.begin(),label.end())+L" Beta";
  Fixture(alpha);Fixture(beta);const auto alphaRoot=g_sDataDir/L"Sites"/alpha,betaRoot=g_sDataDir/L"Sites"/beta;
  const std::wstring url=L"https://example.test/rollback-"+std::wstring(label.begin(),label.end());
  client_notes::NotePage seed;seed.id=11;seed.name=L"Fault recovery";seed.rich=true;seed.rtf=NotesPlainRtf(L"Portal\r\nSeed \u4e2d \U0001f680");
  seed.created=seed.modified=client_notes::Now();seed.active=true;seed.ticket=L"T20261020.0111";seed.status=2;seed.tags={L"rollback",L"keep"};
  seed.links={{L"Portal",url}};seed.linkRangesKnown=true;seed.linkRanges={{0,6,0}};
  Check(client_notes::WriteNotebook(alphaRoot,{seed},client_notes::ReadNotebook(alphaRoot)),"Rollback source commits a private linked note");
  auto other=seed;other.rtf=NotesPlainRtf(L"Other client private text");other.links.clear();other.linkRanges.clear();other.linkRangesKnown=false;
  Check(client_notes::WriteNotebook(betaRoot,{other},client_notes::ReadNotebook(betaRoot)),"Rollback other client owns the same local ID with independent durable text");
  HWND owner=Open(alpha);auto &state=State(owner);HWND edit=Edit(owner);const auto older=Read(edit);
  Select(edit,-1,-1);SendMessageW(edit,EM_STOPGROUPTYPING,0,0);Type(owner,L" older edit");SendMessageW(edit,EM_STOPGROUPTYPING,0,0);
  Check(AwaitTimer(owner) && !state.saveFailed,"An older valid native edit reaches the actual debounce autosave before fault injection");
  const auto before=Read(edit);const auto diskBefore=client_notes::ReadNotebook(alphaRoot).notebook_snapshot.bytes;
  const auto betaBefore=client_notes::ReadNotebook(betaRoot).notebook_snapshot.bytes;
  Check(before.ok && older.ok && SendMessageW(edit,EM_CANUNDO,0,0) && !SendMessageW(edit,EM_CANREDO,0,0) &&
        NotesNativeLinkAddress(edit,state,{0,6})==url,"Fault fixture begins with a real older Undo action and original immutable URL");
  LateStreamFailure fault;fault.before=before;fault.linkOnly=kind==3;bool result=false;
  Check(SetWindowSubclass(edit,FailActualStream,0x524F4C4C,reinterpret_cast<DWORD_PTR>(&fault))!=FALSE,
        "The bounded fault observer is installed only on the actual attached editor");
  if(kind==0) {
    client_notes::Revision revision;revision.rtf=NotesPlainRtf(L"REJECTED RESTORE \u03a9 source");
    result=NotesRestoreText(owner,state,revision);
  } else if(kind==1) {
    Select(edit,0,6);client_notes::NotePage snippet;snippet.rich=true;snippet.rtf=NotesPlainRtf(L"REJECTED SNIPPET \u03a9");
    NotesTemplates library{owner,&state};library.snippets=true;result=NotesInsertSnippet(owner,library,snippet);
  } else if(kind==2) {
    Select(edit,-1,-1);result=NotesInsertSimpleTable(owner,state,2,2);
  } else {
    // A bounded exhaustion fault reaches the actual native label/format edit,
    // then NotesRegisterLinkCookie returns zero and the real binding rejects it.
    // This is explicitly a recovery proof, not a normal-use exhaustion claim.
    Select(edit,0,6);state.editorLinks[state.attachedPageId].nextCookie=LONG_MAX;
    inputs.push_back({L"REJECTED LINK",L"https://example.test/new-rejected-target"});
    Command(owner,IDC_NOTES_LINK);result=lastNotice.find(L"could not be edited safely")==std::wstring::npos;
    fault.observed=fault.observed && inputs.empty() && lastNotice.find(L"previous text and address")!=std::wstring::npos;
  }
  RemoveWindowSubclass(edit,FailActualStream,0x524F4C4C);
  std::cout<<"Rollback injected "<<label<<": observed="<<fault.observed<<" nativeMutation="<<fault.mutated
    <<" bytes="<<fault.delivered<<"/"<<fault.total<<" error="<<fault.error<<" returned="<<result<<std::endl;
  Check(fault.observed && fault.mutated && !result && (kind==3 || (fault.error!=0 && fault.delivered==fault.limit)),
        "The requested failure occurs after a real native edit rather than inside a preview or preflight");
  const auto immediately=Read(edit);const bool canRedo=SendMessageW(edit,EM_CANREDO,0,0)!=0;
  Expect(Same(immediately,before) && NotesNativeLinkAddress(edit,state,{0,6})==url,
         label+": failed transaction immediately restores exact native RTF/raw text and original URL");
  Expect(client_notes::ReadNotebook(alphaRoot).notebook_snapshot.bytes==diskBefore &&
         client_notes::ReadNotebook(betaRoot).notebook_snapshot.bytes==betaBefore,
         label+": failure return preserves both private durable notebooks byte-for-byte");
  const auto redone=SendMessageW(edit,EM_REDO,0,0);const auto afterRedo=Read(edit);
  const bool redoChanged=!Same(afterRedo,before),timer=AwaitTimer(owner);
  const bool diskSafe=Durable(owner,alphaRoot,before,url),otherSafe=client_notes::ReadNotebook(betaRoot).notebook_snapshot.bytes==betaBefore;
  std::cout<<"Rollback Redo "<<label<<": offered="<<canRedo<<" result="<<redone<<" changed="<<redoChanged
    <<" actualTimer="<<timer<<" durableOriginal="<<diskSafe<<" otherClient="<<otherSafe<<std::endl;
  Expect(!canRedo && !redone && !redoChanged,label+": pure Redo cannot resurrect the failed native transaction");
  Expect(!state.saveFailed && diskSafe && otherSafe && (!redoChanged || timer),
         label+": actual autosave never commits rejected content or alters the other client");
  const auto directory=artifacts/fs::path(label);fs::create_directories(directory);
  Put(directory/L"before.rtf",before.rtf);Put(directory/L"native-partial.rtf",fault.partial.rtf);
  Put(directory/L"after-failed-return.rtf",immediately.rtf);Put(directory/L"after-pure-redo.rtf",afterRedo.rtf);
  Put(directory/L"durable-after-redo.ctn",client_notes::ReadNotebook(alphaRoot).notebook_snapshot.bytes);
  // A bad Redo already failed the independent oracle. Remove only that rejected
  // action to inspect the older valid Undo branch in the same before-proof.
  if(redoChanged){SendMessageW(edit,EM_UNDO,0,0);AwaitTimer(owner);}
  Expect(Same(Read(edit),before),label+": the original pre-failure draft is intact before testing older history");
  SendMessageW(edit,EM_UNDO,0,0);const auto oldUndo=Read(edit);const bool olderTimer=AwaitTimer(owner);
  Expect(Same(oldUndo,older) && NotesNativeLinkAddress(edit,state,{0,6})==url,
         label+": one older Undo still removes exactly the earlier valid edit and retains its URL");
  Expect(olderTimer && !state.saveFailed && Durable(owner,alphaRoot,older,url) &&
         client_notes::ReadNotebook(betaRoot).notebook_snapshot.bytes==betaBefore,
         label+": actual autosave after older Undo persists exact original content and client isolation");
  CloseClientNotesDialog(owner,state);NotesBackgroundAudit(("fault recovery "+label).c_str());
}

static bool SameRegistry(const ClientNotesDialogState::LinkRegistry &left,
                         const ClientNotesDialogState::LinkRegistry &right) {
  return left.nextCookie==right.nextCookie && left.useCounter==right.useCounter &&
      left.urls==right.urls && left.lastUse==right.lastUse;
}
static client_notes::NotePage RecoverySeed(const std::wstring &url) {
  client_notes::NotePage seed;seed.id=11;seed.name=L"Retained draft";seed.rich=true;
  seed.rtf=NotesPlainRtf(L"Portal\r\nOriginal \u4e2d \U0001f680");seed.created=seed.modified=client_notes::Now();
  seed.active=true;seed.ticket=L"T20261020.0111";seed.status=2;seed.tags={L"rollback",L"keep"};
  seed.links={{L"Portal",url}};seed.linkRangesKnown=true;seed.linkRanges={{0,6,0}};return seed;
}
static void WrapperEdges() {
  diagnosticCase="wrapper-edges";
  const std::wstring alpha=L"Rollback wrapper Alpha",beta=L"Rollback wrapper Beta",url=L"https://example.test/wrapper-original";
  Fixture(alpha);Fixture(beta);const auto alphaRoot=g_sDataDir/L"Sites"/alpha,betaRoot=g_sDataDir/L"Sites"/beta;
  auto seed=RecoverySeed(url),other=seed;other.rtf=NotesPlainRtf(L"Other isolated wrapper client");
  Check(client_notes::WriteNotebook(alphaRoot,{seed},client_notes::ReadNotebook(alphaRoot)) &&
      client_notes::WriteNotebook(betaRoot,{other},client_notes::ReadNotebook(betaRoot)),"Wrapper edge fixtures commit independent clients with colliding local IDs");
  const auto betaBytes=client_notes::ReadNotebook(betaRoot).notebook_snapshot.bytes;
  HWND owner=Open(alpha);auto &state=State(owner);HWND edit=Edit(owner);const auto original=Read(edit);
  Select(edit,-1,-1);Type(owner,L" older valid edit");SendMessageW(edit,EM_STOPGROUPTYPING,0,0);
  Check(AwaitTimer(owner),"Wrapper edge older edit reaches its actual autosave");
  Type(owner,L" redo candidate");SendMessageW(edit,EM_STOPGROUPTYPING,0,0);const auto redoImage=Read(edit);
  SendMessageW(edit,EM_UNDO,0,0);Check(AwaitTimer(owner),"Wrapper edge actual Undo leaves a durable preexisting Redo candidate");
  const auto before=Read(edit);const auto registry=state.editorLinks[state.attachedPageId];
  const bool modified=SendMessageW(edit,EM_GETMODIFY,0,0)!=0,formatting=state.formattingChanged,metadata=state.metadataChanged;
  Check(SendMessageW(edit,EM_CANUNDO,0,0) && SendMessageW(edit,EM_CANREDO,0,0),"An untouched-failure fixture has both older Undo and legitimate Redo");
  const auto untouched=NotesApplyNativeTransaction(owner,state,CHARRANGE{0,6},[](HWND){return false;});
  Expect(untouched==NotesAppTransactionResult::Recovered && !state.pendingTransactionRecovery && Same(Read(edit),before) &&
      SendMessageW(edit,EM_CANUNDO,0,0) && SendMessageW(edit,EM_CANREDO,0,0),"An untouched rejected collection does not Undo older text or discard legitimate Redo");
  Expect(SameRegistry(registry,state.editorLinks[state.attachedPageId]) &&
      (SendMessageW(edit,EM_GETMODIFY,0,0)!=0)==modified && state.formattingChanged==formatting && state.metadataChanged==metadata,
      "An untouched rejected collection restores the complete original URL registry and dirty flags");
  SendMessageW(edit,EM_REDO,0,0);const bool redoTimer=AwaitTimer(owner);
  Expect(redoTimer && Same(Read(edit),redoImage) && Durable(owner,alphaRoot,redoImage,url),
      "The preserved legitimate Redo still restores exactly its former native text and reaches actual autosave");
  SendMessageW(edit,EM_UNDO,0,0);SendMessageW(edit,EM_UNDO,0,0);const bool olderTimer=AwaitTimer(owner);
  Expect(olderTimer && Same(Read(edit),original) && Durable(owner,alphaRoot,original,url),
      "Older valid Undo remains independent after the untouched failure and legitimate Redo");
  Select(edit,-1,-1);Type(owner,L" valid unsaved draft");SendMessageW(edit,EM_STOPGROUPTYPING,0,0);
  const auto unsaved=Read(edit);const auto durableBefore=client_notes::ReadNotebook(alphaRoot).notebook_snapshot.bytes;
  const auto unsavedRegistry=state.editorLinks[state.attachedPageId];const bool unsavedFormatting=state.formattingChanged,unsavedMetadata=state.metadataChanged;
  bool actualMutation=false;
  const auto exception=NotesApplyNativeTransaction(owner,state,CHARRANGE{-1,-1},[&](HWND target)->bool {
    SendMessageW(target,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L" REJECTED EXCEPTION"));actualMutation=!Same(Read(target),unsaved);
    throw std::runtime_error("Injected post-native-edit exception");
  });
  Expect(actualMutation && exception==NotesAppTransactionResult::Recovered && Same(Read(edit),unsaved) &&
      !SendMessageW(edit,EM_CANREDO,0,0) && !state.pendingTransactionRecovery,
      "A real native mutation followed by an exception restores the exact unsaved preimage and rejects its Redo");
  Expect(SameRegistry(unsavedRegistry,state.editorLinks[state.attachedPageId]) && SendMessageW(edit,EM_GETMODIFY,0,0) &&
      state.formattingChanged==unsavedFormatting && state.metadataChanged==unsavedMetadata &&
      client_notes::ReadNotebook(alphaRoot).notebook_snapshot.bytes==durableBefore,
      "Exception recovery preserves original unsaved dirty state, complete URL registry and preexisting durable bytes");
  const bool unsavedTimer=AwaitTimer(owner);
  Expect(unsavedTimer && Durable(owner,alphaRoot,unsaved,url) && client_notes::ReadNotebook(betaRoot).notebook_snapshot.bytes==betaBytes,
      "The original unsaved draft alone reaches the actual autosave and leaves the other client byte-for-byte unchanged");
  CloseClientNotesDialog(owner,state);NotesBackgroundAudit("wrapper untouched and post-edit exception recovery");
}

static void TerminalUndoRefusal() {
  diagnosticCase="terminal";
  const std::wstring alpha=L"Rollback terminal Alpha",beta=L"Rollback terminal Beta",url=L"https://example.test/terminal-original";
  Fixture(alpha);Fixture(beta);const auto alphaRoot=g_sDataDir/L"Sites"/alpha,betaRoot=g_sDataDir/L"Sites"/beta;
  auto seed=RecoverySeed(url),other=seed;other.rtf=NotesPlainRtf(L"Other isolated terminal client");
  Check(client_notes::WriteNotebook(alphaRoot,{seed},client_notes::ReadNotebook(alphaRoot)) &&
      client_notes::WriteNotebook(betaRoot,{other},client_notes::ReadNotebook(betaRoot)),"Terminal fixtures commit independent clients with colliding local IDs");
  const auto alphaBytes=client_notes::ReadNotebook(alphaRoot).notebook_snapshot.bytes,betaBytes=client_notes::ReadNotebook(betaRoot).notebook_snapshot.bytes;
  HWND owner=Open(alpha);auto &state=State(owner);HWND edit=Edit(owner);
  Select(edit,-1,-1);Type(owner,L" original unsaved terminal draft");SendMessageW(edit,EM_STOPGROUPTYPING,0,0);
  const auto before=Read(edit);const auto registry=state.editorLinks[state.attachedPageId];
  CHARRANGE selection{};NotesSelection(edit,selection);POINT scroll{};SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
  NotesStream rawBefore;EDITSTREAM output{reinterpret_cast<DWORD_PTR>(&rawBefore),0,NotesStreamOut};
  SendMessageW(edit,EM_STREAMOUT,SF_RTF,reinterpret_cast<LPARAM>(&output));
  while(!rawBefore.bytes.empty() && rawBefore.bytes.back()=='\0')rawBefore.bytes.pop_back();
  Check(!output.dwError && !rawBefore.bytes.empty(),"Terminal preimage has an independently read exact raw native RTF image");
  LateStreamFailure fault;fault.before=before;
  Check(SetWindowSubclass(edit,FailActualStream,0x524F4C4C,reinterpret_cast<DWORD_PTR>(&fault))!=FALSE,
      "Terminal Undo-refusal observer is confined to the private attached editor");
  struct NativeUndoRefusal {HWND edit;View original;bool injected=false,called=false,partial=false;HRESULT result=S_OK;LONG count=-1;};
  NativeUndoRefusal refusal{edit,before};
  g_notesAppUndoContext=&refusal;
  g_notesAppUndoObserver=[](HWND target,bool beforeUndo,HRESULT result,LONG count,void *context) {
    auto &observation=*static_cast<NativeUndoRefusal *>(context);if(target!=observation.edit)return;
    if(beforeUndo) {
      observation.partial=!Same(Read(target),observation.original);
      // This point is AFTER the real failed collection closes. Unlike the
      // preserved integrated1 in-stream limit0 assumption, its actual next TOM
      // Undo is now unavailable and is observed independently below.
      SendMessageW(target,EM_SETUNDOLIMIT,0,0);observation.injected=true;
    } else {observation.called=true;observation.result=result;observation.count=count;}
  };
  client_notes::Revision revision;revision.rtf=NotesPlainRtf(L"REJECTED TERMINAL PARTIAL SOURCE");
  const bool result=NotesRestoreText(owner,state,revision);RemoveWindowSubclass(edit,FailActualStream,0x524F4C4C);
  g_notesAppUndoObserver=nullptr;g_notesAppUndoContext=nullptr;
  const auto retained=state.pendingTransactionRecovery;
  std::cout<<"Terminal native Undo: injected="<<refusal.injected<<" actualCall="<<refusal.called<<" partial="<<refusal.partial
      <<" HRESULT="<<refusal.result<<" count="<<refusal.count<<std::endl;
  Expect(!result && fault.observed && fault.mutated && refusal.injected && refusal.called && refusal.partial &&
      refusal.result!=S_OK && refusal.count==0 && retained && IsWindow(owner) && IsWindow(edit),
      "Actual partial mutation followed by native Undo refusal retains an owned recovery preimage instead of claiming success");
  Expect(retained && retained->document.rtf==rawBefore.bytes && retained->view.selection.cpMin==selection.cpMin &&
      retained->view.selection.cpMax==selection.cpMax && retained->view.scroll.x==scroll.x && retained->view.scroll.y==scroll.y &&
      retained->hadRegistry && SameRegistry(retained->registry,registry) && retained->clientRoot==alphaRoot && retained->pageId==11,
      "Terminal recovery owns exact raw unsaved RTF, original view, complete URL registry and original client identity");
  // Consume the original typing's real debounce event before the explicit Save
  // call below (which correctly cancels its timer on the blocked path).
  const bool blockedTimer=AwaitTimer(owner);
  Expect(!state.attachedEditor && !state.attachedPageId && !NotesEditorMatchesActive(state) &&
      !SaveClientNotesDraft(owner,state,false) && !CaptureNotesPage(owner,state,false) && !LoadNotesPage(owner,state),
      "A retained terminal recovery blocks actual save, capture and editor reload without reattaching the partial content");
  Expect(blockedTimer && state.pendingTransactionRecovery==retained && IsWindow(owner) &&
      client_notes::ReadNotebook(alphaRoot).notebook_snapshot.bytes==alphaBytes &&
      client_notes::ReadNotebook(betaRoot).notebook_snapshot.bytes==betaBytes,
      "The real autosave timer cannot publish terminal partial content or modify either client's durable notebook");
  DestroyWindow(owner);NotesBackgroundAudit("terminal native Undo refusal force-close cleanup");
}
}
static void NotesRollbackTransactions(const fs::path &artifacts={}) {
  notes_rollback::Scope scope;notes_rollback::failures=0;
  const auto output=artifacts.empty()?g_sDataDir/L"fault-evidence":artifacts;
  const auto diagnosticDirectory=output/L"native-stages";fs::create_directories(diagnosticDirectory);
  size_t diagnosticIndex=0;
  notes_undo::diagnosticSink=[&](const char *stage,HWND edit,bool result) {
    notes_undo::Document image;const bool captured=notes_undo::CaptureDocument(edit,image);
    CHARRANGE selected{};NotesSelection(edit,selected);
    const auto name=notes_rollback::diagnosticCase+"-"+std::to_string(++diagnosticIndex)+"-"+stage;
    if(captured) {
      Put(diagnosticDirectory/fs::path(name+".rtf"),image.rtf);
      std::string runs;for(const auto &run:image.links)runs+=std::to_string(run.first)+","+std::to_string(run.last)+","+std::to_string(run.cookie)+","+std::to_string(run.effects)+"\n";
      Put(diagnosticDirectory/fs::path(name+".cookies.txt"),runs);
    }
    std::cout<<"Rollback native stage "<<notes_rollback::diagnosticCase<<" "<<stage<<": result="<<result
      <<" captured="<<captured<<" length="<<image.length<<" rtf="<<image.rtf.size()<<" runs="<<image.links.size()
      <<" canUndo="<<SendMessageW(edit,EM_CANUNDO,0,0)<<" canRedo="<<SendMessageW(edit,EM_CANREDO,0,0)
      <<" selection="<<selected.cpMin<<","<<selected.cpMax<<std::endl;
  };
  for(unsigned kind=0;kind<4;++kind)notes_rollback::Run(kind,output);
  notes_rollback::WrapperEdges();notes_rollback::TerminalUndoRefusal();
  std::cout<<"Rollback recovery proof: 4 injected paths; "<<notes_rollback::failures<<" failed expectations."<<std::endl;
  if(notes_rollback::failures)throw std::runtime_error("A rejected native transaction remains redoable or changes its original draft, older history or durable ownership");
}
