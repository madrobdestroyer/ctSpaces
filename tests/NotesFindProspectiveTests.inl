// Native save-budget preflight must inspect a private formatted copy. Refusing
// an expansion and repairing an existing overlong draft must leave the real
// document, immutable URL registry and older native history under user control.
namespace find_prospective {
struct Snapshot {
 std::wstring text;std::string rtf,pages;
 notes_undo::Document native;
 std::map<uint64_t,ClientNotesDialogState::LinkRegistry> links;
 CHARRANGE selected{};POINT scroll{};
 LRESULT undo=0,redo=0,modified=0,limit=0;
 bool formatting=false,metadata=false,failed=false,loading=false,loadFailed=false;
 HWND editor=nullptr,attachedEditor=nullptr;uint64_t attachedPage=0;size_t activePage=0;
 std::wstring clientName;fs::path clientRoot;
 const NotesAppTransactionRecovery *pending=nullptr;
};
static Snapshot Read(HWND owner,HWND inspectedEditor=nullptr){
 const auto edit=inspectedEditor?inspectedEditor:Edit(owner);const auto &state=State(owner);Snapshot value;
 value.text=NotesWindowText(edit);Check(StreamClientNotesOut(edit,value.rtf),"The prospective-budget fixture reads its exact native RTF");
 Check(notes_undo::CaptureDocument(edit,value.native),"The prospective-budget fixture records raw native RTF and exact native cookie/effect runs without normalization");
 Check(client_notes::SerializeNotebook(state.pages,value.pages),"The prospective-budget fixture records the complete saved-page metadata and revisions");
 value.links=state.editorLinks;NotesSelection(edit,value.selected);SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&value.scroll));
 value.undo=SendMessageW(edit,EM_CANUNDO,0,0);value.redo=SendMessageW(edit,EM_CANREDO,0,0);value.modified=SendMessageW(edit,EM_GETMODIFY,0,0);value.limit=SendMessageW(edit,EM_GETLIMITTEXT,0,0);
 value.formatting=state.formattingChanged;value.metadata=state.metadataChanged;value.failed=state.saveFailed;value.loading=state.loading;value.loadFailed=state.editorLoadFailed;
 value.editor=edit;value.attachedEditor=state.attachedEditor;value.attachedPage=state.attachedPageId;value.activePage=state.activePage;value.clientName=state.clientName;value.clientRoot=state.clientRoot;
 value.pending=state.pendingTransactionRecovery.get();return value;
}
static bool RegistriesEqual(const std::map<uint64_t,ClientNotesDialogState::LinkRegistry> &a,const std::map<uint64_t,ClientNotesDialogState::LinkRegistry> &b){
 if(a.size()!=b.size())return false;
 auto left=a.begin(),right=b.begin();for(;left!=a.end();++left,++right){
  if(left->first!=right->first || left->second.nextCookie!=right->second.nextCookie || left->second.useCounter!=right->second.useCounter || left->second.urls!=right->second.urls || left->second.lastUse!=right->second.lastUse)return false;
 }return true;
}
static bool EqualSnapshots(const Snapshot &before,const Snapshot &after){
 return before.text==after.text && before.rtf==after.rtf && before.native.length==after.native.length && before.native.rtf==after.native.rtf && before.native.links==after.native.links && before.pages==after.pages && RegistriesEqual(before.links,after.links) && before.selected.cpMin==after.selected.cpMin && before.selected.cpMax==after.selected.cpMax && before.scroll.x==after.scroll.x && before.scroll.y==after.scroll.y && before.undo==after.undo && before.redo==after.redo && before.modified==after.modified && before.limit==after.limit && before.formatting==after.formatting && before.metadata==after.metadata && before.failed==after.failed && before.loading==after.loading && before.loadFailed==after.loadFailed && before.editor==after.editor && before.attachedEditor==after.attachedEditor && before.attachedPage==after.attachedPage && before.activePage==after.activePage && before.clientName==after.clientName && before.clientRoot==after.clientRoot && before.pending==after.pending;
}
static bool Equal(HWND owner,const Snapshot &before){return EqualSnapshots(before,Read(owner));}
static size_t FirstDifference(const std::string &before,const std::string &after){size_t at=0;while(at<(std::min)(before.size(),after.size())&&before[at]==after[at])++at;return at;}
static std::string NativeRuns(const std::vector<notes_undo::LinkRun> &runs){
 std::string bytes;for(const auto &run:runs)bytes+=std::to_string(run.first)+","+std::to_string(run.last)+","+std::to_string(run.cookie)+","+std::to_string(run.effects)+"\n";return bytes;
}
static bool DiagnoseActualUi(HWND owner,const Snapshot &before,const wchar_t *stage){
 // Retain the original native HWND for diagnostics even when a failed recovery
 // has correctly detached its control ID and refused normal capture.
 const auto after=Read(owner,before.editor);const auto path=g_sDataDir/L"find-actual-ui-recovery"/stage;
 Put(path/L"before-native.rtf",before.native.rtf);Put(path/L"after-native.rtf",after.native.rtf);
 Put(path/L"before-normalized.rtf",before.rtf);Put(path/L"after-normalized.rtf",after.rtf);
 Put(path/L"before-native-runs.txt",NativeRuns(before.native.links));Put(path/L"after-native-runs.txt",NativeRuns(after.native.links));
 Put(path/L"before-notebook.txt",before.pages);Put(path/L"after-notebook.txt",after.pages);
 std::cout<<"Production Find UI components: text="<<(before.text==after.text)<<"; normalizedRTF="<<(before.rtf==after.rtf)<<"; rawRTF="<<(before.native.rtf==after.native.rtf)<<"; nativeLength="<<(before.native.length==after.native.length)<<"; cookieEffects="<<(before.native.links==after.native.links)<<"; registry="<<RegistriesEqual(before.links,after.links)<<"; selection="<<(before.selected.cpMin==after.selected.cpMin&&before.selected.cpMax==after.selected.cpMax)<<"; scroll="<<(before.scroll.x==after.scroll.x&&before.scroll.y==after.scroll.y)<<"; undo="<<(before.undo==after.undo)<<"; redo="<<(before.redo==after.redo)<<"; modified="<<(before.modified==after.modified)<<"; nativeLimit="<<(before.limit==after.limit)<<"; formatting="<<(before.formatting==after.formatting)<<"; metadata="<<(before.metadata==after.metadata)<<"; saveFailed="<<(before.failed==after.failed)<<"; loading="<<(before.loading==after.loading)<<"; loadFailed="<<(before.loadFailed==after.loadFailed)<<"; notebook="<<(before.pages==after.pages)<<"; pending="<<(before.pending==after.pending)<<std::endl;
 std::cout<<"Production Find UI source ownership: nativeEditor="<<(before.editor==after.editor)<<"; attachedEditor="<<(before.attachedEditor==after.attachedEditor)<<"; attachedPage="<<(before.attachedPage==after.attachedPage)<<"; activePage="<<(before.activePage==after.activePage)<<"; client="<<(before.clientName==after.clientName)<<"; root="<<(before.clientRoot==after.clientRoot)<<"; currentControlIsOriginal="<<(GetDlgItem(owner,IDC_CLIENT_NOTES_TEXT)==before.editor)<<"; nativeMatch="<<NotesEditorMatchesActive(State(owner))<<"; pendingNow="<<(after.pending!=nullptr)<<std::endl;
 std::cout<<"Production Find UI native before/after: length="<<before.native.length<<"/"<<after.native.length<<"; rawRTFBytes="<<before.native.rtf.size()<<"/"<<after.native.rtf.size()<<"; firstRawDifference="<<FirstDifference(before.native.rtf,after.native.rtf)<<"; normalizedRTFBytes="<<before.rtf.size()<<"/"<<after.rtf.size()<<"; firstNormalizedDifference="<<FirstDifference(before.rtf,after.rtf)<<"; runs="<<before.native.links.size()<<"/"<<after.native.links.size()<<std::endl;
 std::cout<<"Production Find UI view before/after: selection="<<before.selected.cpMin<<","<<before.selected.cpMax<<"/"<<after.selected.cpMin<<","<<after.selected.cpMax<<"; scroll="<<before.scroll.x<<","<<before.scroll.y<<"/"<<after.scroll.x<<","<<after.scroll.y<<"; undo="<<before.undo<<"/"<<after.undo<<"; redo="<<before.redo<<"/"<<after.redo<<"; modified="<<before.modified<<"/"<<after.modified<<"; limit="<<before.limit<<"/"<<after.limit<<std::endl;
 std::cout<<"Production Find UI flags before/after: formatting="<<before.formatting<<"/"<<after.formatting<<"; metadata="<<before.metadata<<"/"<<after.metadata<<"; saveFailed="<<before.failed<<"/"<<after.failed<<"; loading="<<before.loading<<"/"<<after.loading<<"; loadFailed="<<before.loadFailed<<"/"<<after.loadFailed<<std::endl;
 for(const auto &[id,registry]:before.links){
  const auto found=after.links.find(id);std::cout<<"Production Find UI registry: page="<<id<<"; retained="<<(found!=after.links.end())<<"; beforeNext="<<registry.nextCookie<<"; beforeUse="<<registry.useCounter<<"; beforeUrls="<<registry.urls.size()<<"; beforeLastUse="<<registry.lastUse.size();
  if(found!=after.links.end())std::cout<<"; afterNext="<<found->second.nextCookie<<"; afterUse="<<found->second.useCounter<<"; afterUrls="<<found->second.urls.size()<<"; afterLastUse="<<found->second.lastUse.size()<<"; exactUrls="<<(registry.urls==found->second.urls)<<"; exactLastUse="<<(registry.lastUse==found->second.lastUse);std::cout<<std::endl;
 }
 std::wcout<<L"Production Find UI notice: "<<State(owner).findNotice<<L"; rendered="<<FindInfo(owner)<<L"; saveDetail="<<State(owner).saveProblemDetail<<L"; artifacts="<<path.wstring()<<std::endl;
 return EqualSnapshots(before,after);
}
static bool AwaitAutosave(HWND owner){
 const auto deadline=GetTickCount64()+1600;
 while(GetTickCount64()<deadline){
  MSG event{};while(PeekMessageW(&event,owner,WM_TIMER,WM_TIMER,PM_REMOVE)){
   const bool autosave=event.wParam==1;DispatchMessageW(&event);if(autosave)return true;
  }
  MsgWaitForMultipleObjectsEx(0,nullptr,25,QS_TIMER,MWMO_INPUTAVAILABLE);
 }return false;
}
static std::vector<LONG> RowProperties(ITextRange *range){
 std::vector<LONG> values;ITextRange2 *extended=nullptr;ITextRow *row=nullptr;
 const auto queried=range->QueryInterface(__uuidof(ITextRange2),reinterpret_cast<void **>(&extended));values.push_back(queried);
 const auto found=extended?extended->GetRow(&row):E_NOINTERFACE;values.push_back(found);
 if(row){
  const auto add=[&](auto member){LONG value=LONG_MIN;values.push_back((row->*member)(&value));values.push_back(value);};
  add(&ITextRow::GetAlignment);add(&ITextRow::GetCellCount);add(&ITextRow::GetCellIndex);add(&ITextRow::GetCellMargin);add(&ITextRow::GetHeight);add(&ITextRow::GetIndent);add(&ITextRow::GetNestLevel);add(&ITextRow::GetCellAlignment);add(&ITextRow::GetCellColorBack);add(&ITextRow::GetCellColorFore);add(&ITextRow::GetCellMergeFlags);add(&ITextRow::GetCellShading);add(&ITextRow::GetCellVerticalText);add(&ITextRow::GetCellWidth);
  LONG border[4]={LONG_MIN,LONG_MIN,LONG_MIN,LONG_MIN};values.push_back(row->GetCellBorderColors(&border[0],&border[1],&border[2],&border[3]));values.insert(values.end(),std::begin(border),std::end(border));
  std::fill(std::begin(border),std::end(border),LONG_MIN);values.push_back(row->GetCellBorderWidths(&border[0],&border[1],&border[2],&border[3]));values.insert(values.end(),std::begin(border),std::end(border));
 }
 if(row)row->Release();if(extended)extended->Release();return values;
}
static bool DiagnoseClone(HWND source,NotesFindProspectiveCopy &copy,const std::string &original,const std::string &cloned){
 const auto path=g_sDataDir/L"prospective-clone-diagnostic";Put(path/L"source.rtf",original);Put(path/L"copy.rtf",cloned);
 const auto raw=NotesRawEditorText(source),other=copy.edit?NotesRawEditorText(copy.edit):std::nullopt;
 std::string rawBytes,otherBytes;if(raw)client_notes::EncodeUtf8(*raw,rawBytes);if(other)client_notes::EncodeUtf8(*other,otherBytes);Put(path/L"source-native.txt",rawBytes);Put(path/L"copy-native.txt",otherBytes);
 const size_t common=(std::min)(original.size(),cloned.size());size_t difference=0;while(difference<common&&original[difference]==cloned[difference])++difference;
 unsigned positions=0,fontDifferences=0,paragraphDifferences=0,rowDifferences=0,cookieDifferences=0;
 if(copy.source&&copy.document&&copy.copiedStory>0&&copy.copiedStory<=512)for(LONG at=0;at<copy.copiedStory;++at){
  ITextRange *a=nullptr,*b=nullptr;ITextFont *af=nullptr,*bf=nullptr;ITextPara *ap=nullptr,*bp=nullptr;LONG fontSame=tomFalse,paraSame=tomFalse;
  const bool ranges=SUCCEEDED(copy.source->Range(at,at+1,&a))&&SUCCEEDED(copy.document->Range(at,at+1,&b));
  const bool fonts=ranges&&SUCCEEDED(a->GetFont(&af))&&SUCCEEDED(b->GetFont(&bf))&&SUCCEEDED(af->IsEqual(bf,&fontSame))&&fontSame==tomTrue;
  const bool paragraphs=ranges&&SUCCEEDED(a->GetPara(&ap))&&SUCCEEDED(b->GetPara(&bp))&&SUCCEEDED(ap->IsEqual(bp,&paraSame))&&paraSame==tomTrue;
  const bool rows=ranges&&RowProperties(a)==RowProperties(b);
  LONG ac=0,bc=0;bool ar=false,br=false;const bool al=NotesRangeLinkEffects(source,{at,at+1},nullptr,&ac,false,&ar),bl=NotesRangeLinkEffects(copy.edit,{at,at+1},nullptr,&bc,false,&br);
  const bool cookies=ar&&br&&ac==bc&&al==bl;
  ++positions;fontDifferences+=!fonts;paragraphDifferences+=!paragraphs;rowDifferences+=!rows;cookieDifferences+=!cookies;
  if(!fonts||!paragraphs||!rows||!cookies)std::cout<<"Prospective clone semantic position: cp="<<at<<"; font="<<fonts<<"; paragraph="<<paragraphs<<"; row/cell="<<rows<<"; cookie/link="<<cookies<<std::endl;
  if(bp)bp->Release();if(ap)ap->Release();if(bf)bf->Release();if(af)af->Release();if(b)b->Release();if(a)a->Release();
 }
 std::cout<<"Prospective clone semantics: rawNativeText="<<(raw&&other&&*raw==*other)<<"; positions="<<positions<<"; fontDifferences="<<fontDifferences<<"; paragraphDifferences="<<paragraphDifferences<<"; rowCellDifferences="<<rowDifferences<<"; cookieLinkDifferences="<<cookieDifferences<<"; sourceRTFBytes="<<original.size()<<"; copyRTFBytes="<<cloned.size()<<"; firstRTFDifference="<<difference<<std::endl;
 std::wcout<<L"Prospective private clone diagnostics: "<<path.wstring()<<std::endl;
 return raw&&other&&*raw==*other&&copy.copiedStory>0&&positions==static_cast<unsigned>(copy.copiedStory)&&!fontDifferences&&!paragraphDifferences&&!rowDifferences&&!cookieDifferences;
}
struct CloneFailure {
 HWND live=nullptr,copy=nullptr;unsigned attempts=0;LONG before=0,after=0;
 bool installed=false,mutated=false,rejected=false,undoRejected=false,destroyed=false;
};
static constexpr UINT_PTR kCloneFailureSubclass=0x4e465043;
static LRESULT CALLBACK RefuseCloneReplacement(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){
 auto &fault=*reinterpret_cast<CloneFailure *>(data);
 if(message==EM_REPLACESEL){
  if(++fault.attempts>=2){fault.rejected=true;return 0;}
  fault.before=notes_find::Length(window);const auto result=DefSubclassProc(window,message,w,l);fault.after=notes_find::Length(window);fault.mutated=fault.after==fault.before+1;return result;
 }
 if((message==EM_UNDO||message==WM_UNDO)&&fault.rejected){fault.undoRejected=true;return FALSE;}
 if(message==WM_NCDESTROY){fault.destroyed=true;fault.copy=nullptr;RemoveWindowSubclass(window,RefuseCloneReplacement,id);}
 return DefSubclassProc(window,message,w,l);
}
struct CloneFailureScope {
 CloneFailure fault;std::function<void(HWND,HWND,int,int)> previous;
 explicit CloneFailureScope(HWND live):previous(std::move(notesRichEditCreatedObserver)){
  fault.live=live;
  notesRichEditCreatedObserver=[this](HWND window,HWND parent,int width,int height){
   DWORD pid=0;GetWindowThreadProcessId(window,&pid);
   if(fault.installed||parent||width!=1||height!=1||window==fault.live||pid!=GetCurrentProcessId()||GetAncestor(window,GA_ROOT)!=window||!(GetWindowLongPtrW(window,GWL_STYLE)&WS_POPUP)||(GetWindowLongPtrW(window,GWL_STYLE)&WS_VISIBLE)||!(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_NOACTIVATE))return;
   fault.copy=window;fault.installed=SetWindowSubclass(window,RefuseCloneReplacement,kCloneFailureSubclass,reinterpret_cast<DWORD_PTR>(&fault))!=FALSE;
  };
 }
 ~CloneFailureScope(){notesRichEditCreatedObserver=std::move(previous);if(fault.copy&&IsWindow(fault.copy))RemoveWindowSubclass(fault.copy,RefuseCloneReplacement,kCloneFailureSubclass);}
};
static void FailedCloneCases(HWND owner){
 using Status=notes_find::ReplaceStatus;
 auto &state=State(owner);const auto edit=Edit(owner);const auto before=Read(owner);
 {
  CloneFailureScope injection(edit);NotesFindProspectiveCopy copy;
  Check(NotesFindCloneForReplacement(edit,copy),"The bounded failure fixture creates a disposable private clone of the real source editor");
  const auto result=notes_find::Replace(copy.edit,L"q",L"qq",NotesFindOptions(state),true);
  std::cout<<"Prospective raw native failure: installed="<<injection.fault.installed<<"; attempts="<<injection.fault.attempts<<"; nativeMutation="<<injection.fault.mutated<<"; cloneUndoRefused="<<injection.fault.undoRejected<<"; count="<<result.count<<"; rolledBack="<<result.rolledBack<<std::endl;
  Check(injection.fault.installed&&injection.fault.mutated&&injection.fault.rejected&&injection.fault.undoRejected&&result.status==Status::Failed&&result.count==1&&!result.rolledBack,"One actual private native replacement followed by refused replacement and Undo produces a Failed/count1/unrestored disposable copy");
 }
 Check(!notesRichEditCreatedObserver&&Equal(owner,before),"The fault observer disarms on scope exit and the raw private failure leaves the entire live snapshot unchanged");
 {
  CloneFailureScope injection(edit);const auto result=NotesFindProspectiveReplacement(edit,state,L"q",L"qq",true);
  Check(injection.fault.installed&&injection.fault.mutated&&injection.fault.undoRejected&&injection.fault.destroyed&&result.status==Status::Unavailable&&!result.count&&!result.rolledBack,"Preflight normalizes a genuinely failed unrestored private copy to Unavailable with no alleged live edit count or rollback");
 }
 Check(!notesRichEditCreatedObserver&&Equal(owner,before),"Normalizing the failed clone preserves the live RTF, Undo/Redo, selection, dirty flags, pages and immutable URL registry");
 FindQuery(owner,L"q");Command(owner,IDC_NOTES_REPLACE);SetDlgItemTextW(owner,IDC_NOTES_REPLACE_TEXT,L"qq");const auto uiBefore=Read(owner);
 {
  CloneFailureScope injection(edit);Command(owner,IDC_NOTES_REPLACE_ALL);
  Check(injection.fault.installed&&injection.fault.mutated&&injection.fault.undoRejected&&injection.fault.destroyed,"The actual Replace all UI exercises a native partial failure confined to its disposable private copy");
 }
 Check(!notesRichEditCreatedObserver&&FindInfo(owner).find(L"Could not prepare replacement; no text changed")!=std::wstring::npos&&Equal(owner,uiBefore),"A failed private preflight reports no live change and never marks, captures, repairs or mutates the source document");
}
struct LiveFailure {
 HWND owner=nullptr,edit=nullptr;unsigned attempts=0;uint64_t initialUse=0;
 bool installed=false,mutated=false,rejected=false,loadingObserved=false,cookieUseTouched=false,refuseUndo=false,undoRejected=false;
};
static constexpr UINT_PTR kLiveFailureSubclass=0x4e464c52;
static LRESULT CALLBACK RefuseSecondLiveReplacement(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){
 auto &fault=*reinterpret_cast<LiveFailure *>(data);
 if((message==EM_UNDO||message==WM_UNDO)&&fault.rejected&&fault.refuseUndo){fault.undoRejected=true;return FALSE;}
 if(message==EM_REPLACESEL){
  if(++fault.attempts==2){fault.rejected=true;return 0;}
  if(fault.attempts==1){
   auto &state=State(fault.owner);fault.loadingObserved=state.loading;
   const auto registry=state.editorLinks.find(state.attachedPageId);fault.cookieUseTouched=registry!=state.editorLinks.end()&&registry->second.useCounter>fault.initialUse;
   const auto before=NotesWindowText(window);const auto result=DefSubclassProc(window,message,w,l);fault.mutated=NotesWindowText(window)!=before;return result;
  }
 }
 if(message==WM_NCDESTROY){fault.edit=nullptr;RemoveWindowSubclass(window,RefuseSecondLiveReplacement,id);}
 return DefSubclassProc(window,message,w,l);
}
struct LiveFailureScope {
 LiveFailure fault;
 explicit LiveFailureScope(HWND owner,bool refuseUndo=false){
  fault.owner=owner;fault.edit=Edit(owner);fault.refuseUndo=refuseUndo;const auto &state=State(owner);const auto registry=state.editorLinks.find(state.attachedPageId);fault.initialUse=registry==state.editorLinks.end()?0:registry->second.useCounter;
  DWORD pid=0;GetWindowThreadProcessId(fault.edit,&pid);
  if(backgroundNotesTests&&pid==GetCurrentProcessId()&&NotesEditorMatchesActive(state)&&state.attachedEditor==fault.edit)fault.installed=SetWindowSubclass(fault.edit,RefuseSecondLiveReplacement,kLiveFailureSubclass,reinterpret_cast<DWORD_PTR>(&fault))!=FALSE;
 }
 ~LiveFailureScope(){if(fault.edit&&IsWindow(fault.edit))RemoveWindowSubclass(fault.edit,RefuseSecondLiveReplacement,kLiveFailureSubclass);}
};
static void FailedLiveUiCase(HWND owner){
 FindQuery(owner,L"q");Command(owner,IDC_NOTES_REPLACE);SetDlgItemTextW(owner,IDC_NOTES_REPLACE_TEXT,L"r");const auto before=Read(owner);
 {
  LiveFailureScope injection(owner);Command(owner,IDC_NOTES_REPLACE_ALL);
  std::cout<<"Production Find UI failure: installed="<<injection.fault.installed<<"; attempts="<<injection.fault.attempts<<"; liveNativeMutation="<<injection.fault.mutated<<"; secondRefused="<<injection.fault.rejected<<"; loadingDuringNativeEdit="<<injection.fault.loadingObserved<<"; registryUseTouched="<<injection.fault.cookieUseTouched<<std::endl;
  Check(injection.fault.installed&&injection.fault.mutated&&injection.fault.rejected&&injection.fault.loadingObserved&&injection.fault.cookieUseTouched,"The real Replace all UI reaches one attached-editor native mutation under production loading suppression and touches its actual link-use registry before a bounded second-match refusal");
 }
 const bool exactlyRestored=DiagnoseActualUi(owner,before,L"native-second-refusal");
 Check(FindInfo(owner).find(L"changes undone")!=std::wstring::npos&&exactlyRestored,"The production UI Recovery callback restores exact raw RTF, cookie/effect runs, app registry counters and URLs, selection, Undo/Redo, dirty flags and notebook metadata after a real partial edit");
 Command(owner,IDC_NOTES_REDO);
 Check(DiagnoseActualUi(owner,before,L"redo-after-refusal"),"A pure Redo after the failed real UI replacement cannot resurrect any rejected text or link mutation");
}
static void TerminalLiveUiCase(HWND returnOwner){
 const std::wstring name=L"Find terminal retained source",otherName=L"Find terminal isolated client",address=L"https://example.test/terminal?source=find";
 Fixture(name);const auto owner=Open(name);auto &state=State(owner);Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Support alpha alpha alpha");
 Select(Edit(owner),0,7);inputs.push_back({L"Support",address});Command(owner,IDC_NOTES_LINK);Save(owner);
 SendMessageW(Edit(owner),EM_STOPGROUPTYPING,0,0);Select(Edit(owner),LONG_MAX,LONG_MAX);Type(owner,L" prior");SendMessageW(Edit(owner),EM_STOPGROUPTYPING,0,0);Save(owner);
 Fixture(otherName);const auto otherRoot=g_sDataDir/L"Sites"/otherName;
 client_notes::NotePage other{L"Private",true,NotesPlainRtf(L"Keep the independent terminal-proof notebook unchanged."),{}};other.id=1;other.open=true;other.active=true;
 Check(client_notes::WriteNotebook(otherRoot,{other},client_notes::ReadNotebook(otherRoot)),"The terminal Find proof seeds an independent durable notebook");
 const auto otherBytes=client_notes::ReadNotebook(otherRoot).notebook_snapshot.bytes;
 FindQuery(owner,L"alpha");Command(owner,IDC_NOTES_REPLACE);SetDlgItemTextW(owner,IDC_NOTES_REPLACE_TEXT,L"beta");const auto before=Read(owner);const auto sourceRoot=state.clientRoot;const auto sourceBytes=client_notes::ReadNotebook(sourceRoot).notebook_snapshot.bytes;const auto damagedEditor=Edit(owner);const auto pageId=state.attachedPageId;
 {
  LiveFailureScope injection(owner,true);Command(owner,IDC_NOTES_REPLACE_ALL);
  Check(injection.fault.installed&&injection.fault.mutated&&injection.fault.rejected&&injection.fault.undoRejected&&injection.fault.loadingObserved,"A separate actualUI terminal proof performs one native source edit, refuses its second match and refuses native Undo under production loading suppression");
 }
 const auto retained=state.pendingTransactionRecovery;
 Check(retained&&retained->modelDocument&&retained->owner==owner&&retained->edit==damagedEditor&&retained->clientName==name&&retained->clientRoot==sourceRoot&&retained->pageId==pageId&&retained->document.length==before.native.length&&retained->document.rtf==before.native.rtf&&retained->document.links==before.native.links&&retained->modelDocument->length==before.native.length&&retained->modelDocument->rtf==before.native.rtf&&retained->modelDocument->links==before.native.links,"An unrecoverable actualUI edit retains both exact raw native preimages and their original client, note and HWND ownership after Model and UI locals return");
 Check(retained->hadRegistry&&before.links.contains(pageId)&&retained->registry.urls==before.links.at(pageId).urls&&retained->registry.nextCookie==before.links.at(pageId).nextCookie&&retained->registry.useCounter==before.links.at(pageId).useCounter&&retained->registry.lastUse==before.links.at(pageId).lastUse,"Terminal recovery retains the complete original immutable link registry and use metadata rather than the partially edited live registry");
 Check(!state.attachedEditor&&!state.attachedPageId&&!NotesEditorMatchesActive(state)&&GetDlgItem(owner,IDC_CLIENT_NOTES_TEXT)!=damagedEditor&&(GetWindowLongPtrW(damagedEditor,GWL_STYLE)&ES_READONLY)&&state.saveFailed&&state.editorLoadFailed,"The unverifiable native editor is detached from the note identity and becomes read-only with saving paused");
 Check(FindInfo(owner).find(L"original draft is retained")!=std::wstring::npos&&FindInfo(owner).find(L"Editing and saving are paused")!=std::wstring::npos&&FindInfo(owner).find(L"use Undo")==std::wstring::npos,"Terminal Find feedback accurately explains retained original content and paused editing/saving without offering an unsafe Undo action");
 Check(!CaptureNotesPage(owner,state,false)&&!SaveClientNotesDraft(owner,state,false)&&state.pendingTransactionRecovery==retained,"Normal capture and save cannot promote the detached damaged editor over its retained original draft");
 const auto creates=explicitRichEditCreates;
 Check(!LoadNotesPage(owner,state)&&explicitRichEditCreates==creates&&state.pendingTransactionRecovery==retained,"Loading another native editor cannot silently replace or release the retained transaction preimage");
 Check(!SwitchClientNotes(owner,state,otherName,otherRoot)&&state.clientName==name&&state.clientRoot==sourceRoot&&state.pendingTransactionRecovery==retained,"A client switch cannot move an unverifiable source edit or its retained draft into a different client's notebook");
 Check(client_notes::ReadNotebook(sourceRoot).notebook_snapshot.bytes==sourceBytes&&client_notes::ReadNotebook(otherRoot).notebook_snapshot.bytes==otherBytes,"Blocked capture, save, reload and switch leave both source and other client durable notebook bytes unchanged");
 DestroyWindow(owner);g_hNotesWindow=returnOwner;
 Check(retained->document.rtf==before.native.rtf&&retained->modelDocument->rtf==before.native.rtf,"The retained preimages have strong ownership independent of the completed native failure and test-window lifetime");
 NotesBackgroundAudit("actual Find UI terminal ownership and isolation");
}
}
static void FindProspectiveBudgetCases(HWND owner){
 using Status=notes_find::ReplaceStatus;
 auto &state=State(owner);const auto edit=Edit(owner);state.findMatchCase=true;state.findWholeWord=false;state.findWrap=true;
 SetWindowTextW(edit,L"");Select(edit,0,0);Type(owner,L"Mixed native formatting\r\n");Select(edit,0,5);Command(owner,IDC_NOTES_BOLD);Select(edit,LONG_MAX,LONG_MAX);
 NotesCodeInput code;code.language=1;code.source=L"Get-Printer -ComputerName PRINT01";
 Check(NotesInsertCode(owner,state,code),"The prospective clone fixture includes a real production native code table");
 Select(edit,LONG_MAX,LONG_MAX);Type(owner,L"Trailing paragraph\r\n");
 PARAFORMAT2 finalParagraph{sizeof(finalParagraph)};finalParagraph.dwMask=PFM_SPACEAFTER|PFM_LINESPACING;finalParagraph.dySpaceAfter=140;finalParagraph.bLineSpacingRule=5;finalParagraph.dyLineSpacing=28;
 SendMessageW(edit,EM_SETPARAFORMAT,0,reinterpret_cast<LPARAM>(&finalParagraph));Save(owner);
 const auto formatted=find_prospective::Read(owner);
 {
  NotesFindProspectiveCopy copy;const bool cloned=NotesFindCloneForReplacement(edit,copy);std::string rtf;
  const bool exported=cloned&&StreamClientNotesOut(copy.edit,rtf),sameText=cloned&&NotesWindowText(copy.edit)==formatted.text,sameRtf=exported&&rtf==formatted.rtf,exact=sameRtf&&sameText;
  std::cout<<"Prospective formatted/table clone: created="<<cloned<<"; exactRTF="<<sameRtf<<"; exactText="<<sameText<<"; stage="<<copy.stage<<"; HRESULT="<<static_cast<unsigned long>(copy.status)<<"; sourceStory="<<copy.sourceStory<<"; copiedStory="<<copy.copiedStory<<std::endl;
  const bool semantics=find_prospective::DiagnoseClone(edit,copy,formatted.rtf,rtf);
  Check(exact&&semantics&&rtf.find("\\trowd")!=std::string::npos&&rtf.find("\\intbl")!=std::string::npos,"The native clone retains exact RTF and every native font, paragraph, table-cell, cookie and text position including final paragraph spacing");
 }
 Check(find_prospective::Equal(owner,formatted),"Cloning native tables and final paragraph formatting preserves the live document, history, view and all notebook metadata");
 const std::wstring label(2048,L'q'),address=L"https://example.test/prospective?client=source&immutable=1";
 SetWindowTextW(edit,L"");Select(edit,0,0);Type(owner,(label+L" end").c_str());Select(edit,0,2048);inputs.push_back({label,address});Command(owner,IDC_NOTES_LINK);Save(owner);
 Check(NotesNativeLinkAddress(edit,state,{0,2048})==address,"The prospective fixture begins with the maximum saved label and its exact immutable destination");
 SendMessageW(edit,EM_EMPTYUNDOBUFFER,0,0);Select(edit,LONG_MAX,LONG_MAX);Type(owner,L" prior");SendMessageW(edit,EM_STOPGROUPTYPING,0,0);Save(owner);
 const auto sourceRoot=state.clientRoot;const auto sourceName=state.clientName;const auto sourceId=state.attachedPageId;
 const std::wstring otherName=L"Prospective isolated client";Fixture(otherName);const auto otherRoot=g_sDataDir/L"Sites"/otherName;
 client_notes::NotePage other{L"Isolated",true,NotesPlainRtf(L"Independent prospective-budget sentinel."),{}};other.id=1;other.open=true;other.active=true;
 Check(client_notes::WriteNotebook(otherRoot,{other},client_notes::ReadNotebook(otherRoot)),"The prospective proof creates a separate durable client notebook");
 const auto otherBytes=client_notes::ReadNotebook(otherRoot).notebook_snapshot.bytes;
 // Cursor persistence is ordinary metadata, so commit the intended selection
 // before byteexact refusal checks rather than comparing against an old view.
 Select(edit,128,129);Save(owner);const auto sourceBytes=client_notes::ReadNotebook(sourceRoot).notebook_snapshot.bytes;
 const auto baseline=find_prospective::Read(owner);
 {
  NotesFindProspectiveCopy copy;const bool cloned=NotesFindCloneForReplacement(edit,copy);std::string clonedRtf;
  const bool exported=cloned&&StreamClientNotesOut(copy.edit,clonedRtf);LONG nativeCookie=0,clonedCookie=0;
  NotesRangeLinkEffects(edit,{0,2048},nullptr,&nativeCookie);if(cloned)NotesRangeLinkEffects(copy.edit,{0,2048},nullptr,&clonedCookie);
  LONG sourceFirst=-1,sourceLast=-1,copiedFirst=-1,copiedLast=-1;
  if(copy.from){copy.from->GetStart(&sourceFirst);copy.from->GetEnd(&sourceLast);}if(copy.to){copy.to->GetStart(&copiedFirst);copy.to->GetEnd(&copiedLast);}
  std::cout<<"Prospective clone diagnostic: stage="<<copy.stage<<"; HRESULT="<<static_cast<unsigned long>(copy.status)<<"; sourceStory="<<copy.sourceStory<<"; copiedStory="<<copy.copiedStory<<"; sourceRange="<<sourceFirst<<","<<sourceLast<<"; copiedRange="<<copiedFirst<<","<<copiedLast<<"; window="<<(copy.edit!=nullptr)<<"; sourceDoc="<<(copy.source!=nullptr)<<"; copyDoc="<<(copy.document!=nullptr)<<std::endl;
  std::cout<<"Prospective native clone: created="<<cloned<<"; exactRTF="<<(exported&&clonedRtf==baseline.rtf)<<"; exactText="<<(cloned&&NotesWindowText(copy.edit)==baseline.text)<<"; sourceCookie="<<nativeCookie<<"; copiedCookie="<<clonedCookie<<std::endl;
  Check(cloned&&exported&&clonedRtf==baseline.rtf&&NotesWindowText(copy.edit)==baseline.text,"A complete native formatted clone retains exact RTF, text and final-paragraph state");
  CHARRANGE copied{};NotesSelection(copy.edit,copied);
  Check(nativeCookie>0&&clonedCookie==nativeCookie&&copied.cpMin==baseline.selected.cpMin&&copied.cpMax==baseline.selected.cpMax&&SendMessageW(copy.edit,EM_GETLIMITTEXT,0,0)==baseline.limit,"The private clone retains native cookies, replacement selection and original typing limit");
 }
 Check(find_prospective::Equal(owner,baseline),"Creating and destroying the private clone leaves the live RTF, selection, Undo/Redo, dirty flags, complete page metadata and URL registry unchanged");
 const auto expansion=NotesFindProspectiveReplacement(edit,state,L"q",L"qq",true);
 Check(expansion.status==Status::TooLarge&&find_prospective::Equal(owner,baseline),"A prospective 4096-character link label is refused before any live edit, capture, registry pruning or Undo mutation");
 const auto noMatch=NotesFindProspectiveReplacement(edit,state,L"absent phrase",L"replacement",true);
 const auto noChange=NotesFindProspectiveReplacement(edit,state,L"qq",L"qq",true);
 Check(noMatch.status==Status::NoMatch&&noChange.status==Status::NoChange&&find_prospective::Equal(owner,baseline),"No-match and identical-text preflights retain their ordinary results without touching the live document or history");
 find_prospective::FailedCloneCases(owner);
 find_prospective::FailedLiveUiCase(owner);
 FindQuery(owner,L"q");Command(owner,IDC_NOTES_REPLACE);SetDlgItemTextW(owner,IDC_NOTES_REPLACE_TEXT,L"qq");const auto beforeCommand=find_prospective::Read(owner);Command(owner,IDC_NOTES_REPLACE_ALL);
 Check(FindInfo(owner).find(L"too large")!=std::wstring::npos&&find_prospective::Equal(owner,beforeCommand),"The real Replace all command explains a persistent link-label budget refusal inline and leaves the entire live snapshot unchanged");
 SendMessageW(owner,WM_TIMER,1,0);
 const auto afterRefusal=client_notes::ReadNotebook(sourceRoot).notebook_snapshot.bytes;const bool otherExact=client_notes::ReadNotebook(otherRoot).notebook_snapshot.bytes==otherBytes;
 Put(g_sDataDir/L"prospective-clone-diagnostic"/L"refusal-source-before.ctn",sourceBytes);Put(g_sDataDir/L"prospective-clone-diagnostic"/L"refusal-source-after.ctn",afterRefusal);
 std::cout<<"Prospective refusal save handler: saveFailed="<<state.saveFailed<<"; sourceBytesExact="<<(afterRefusal==sourceBytes)<<"; otherClientBytesExact="<<otherExact<<"; storedCursor="<<state.pages[state.activePage].cursor<<std::endl;
 Check(!state.saveFailed&&afterRefusal==sourceBytes&&otherExact,"The production autosave handler after a refused replacement preserves both clients' exact durable notebook bytes");
 Command(owner,IDC_NOTES_UNDO);Check(NotesWindowText(edit)==label+L" end"&&NotesNativeLinkAddress(edit,state,{0,2048})==address,"Refusing a prospective expansion leaves the user's preceding native Undo action and exact link target intact");
 Command(owner,IDC_NOTES_REDO);Check(NotesWindowText(edit)==baseline.text&&NotesNativeLinkAddress(edit,state,{0,2048})==address,"Redo after refusal reapplies only the user's preceding edit and retains its original address");Save(owner);
 // This draft is intentionally created by an ordinary native edit, without a
 // save/capture. Preflight must not reject it before applying its shortening.
 Select(edit,0,2048);const std::wstring overlong(4096,L'q');Type(owner,overlong.c_str());SendMessageW(edit,EM_STOPGROUPTYPING,0,0);
 Check(NotesWindowText(edit)==overlong+L" end prior"&&NotesNativeLinkAddress(edit,state,{0,4096})==address,"A real native edit creates an overlong linked draft whose original cookie still identifies the exact address");
 const auto stored=state.pages[state.activePage];const auto durableBeforeFailure=client_notes::ReadNotebook(sourceRoot).notebook_snapshot.bytes;
 const bool savedOverlong=SaveClientNotesDraft(owner,state,false);const auto &afterFailedCapture=state.pages[state.activePage];
 Check(!savedOverlong&&state.saveFailed&&client_notes::ReadNotebook(sourceRoot).notebook_snapshot.bytes==durableBeforeFailure&&afterFailedCapture.rtf==stored.rtf&&afterFailedCapture.links==stored.links&&afterFailedCapture.linkRanges==stored.linkRanges&&afterFailedCapture.linkRangesKnown==stored.linkRangesKnown&&NotesWindowText(edit)==overlong+L" end prior"&&NotesNativeLinkAddress(edit,state,{0,4096})==address,"Capturing an existing overlong linked draft fails closed, retains the stored RTF and exact mappings and durable bytes, and keeps its complete live draft and URL available for repair");
 const std::wstring repairedLabel(2048,L'r'),repairedText=repairedLabel+L" end prior";
 const auto oversized=find_prospective::Read(owner);const auto recovery=NotesFindProspectiveReplacement(edit,state,L"qq",L"r",true);
 Check(recovery.status==Status::Changed&&recovery.count==2048&&find_prospective::Equal(owner,oversized),"Preflight can shorten an existing 4096-character linked draft to the valid limit without capturing or changing the live oversized draft");
 FindQuery(owner,L"qq");Command(owner,IDC_NOTES_REPLACE);SetDlgItemTextW(owner,IDC_NOTES_REPLACE_TEXT,L"r");Command(owner,IDC_NOTES_REPLACE_ALL);
 Check(NotesWindowText(edit)==repairedText&&NotesNativeLinkAddress(edit,state,{0,2048})==address&&FindInfo(owner).find(L"Replaced 2048 matches")!=std::wstring::npos,"The actual shortening command repairs the oversized draft with new content and retains the complete immutable link target");
 const bool timer=find_prospective::AwaitAutosave(owner);const auto saved=client_notes::ReadNotebook(sourceRoot);const auto page=std::find_if(saved.pages.begin(),saved.pages.end(),[&](const auto &value){return value.id==sourceId;});
 Check(timer&&!state.saveFailed&&saved.notebook_snapshot.bytes!=sourceBytes&&page!=saved.pages.end()&&page->links==std::vector<std::pair<std::wstring,std::wstring>>{{repairedLabel,address}}&&page->linkRanges==std::vector<client_notes::LinkRange>{{0,2048,0}}&&page->linkRangesKnown,"An actual queued debounce timer commits new notebook bytes containing the repaired label, destination and occurrence range");
 const auto reopened=Open(sourceName);Check(NotesWindowText(Edit(reopened))==repairedText&&NotesNativeLinkAddress(Edit(reopened),State(reopened),{0,2048})==address,"A fresh client reopen reads the newly committed repaired draft and exact immutable destination");DestroyWindow(reopened);g_hNotesWindow=owner;
 Check(client_notes::ReadNotebook(otherRoot).notebook_snapshot.bytes==otherBytes,"Refusal, recovery, autosave and fresh reopen leave the independent client's entire notebook byte-for-byte unchanged");
 Command(owner,IDC_NOTES_UNDO);Check(NotesWindowText(edit)==oversized.text&&NotesNativeLinkAddress(edit,state,{0,4096})==address,"One native Undo of the shortening command restores the exact oversized draft and its destination");
 Command(owner,IDC_NOTES_UNDO);Check(NotesWindowText(edit)==baseline.text&&NotesNativeLinkAddress(edit,state,{0,2048})==address,"The user's older edit remains reachable after the successful preflight and shortening transaction");Save(owner);
 find_prospective::TerminalLiveUiCase(owner);
 std::cout<<"Prospective save-budget proof: maximum-label refusal, exact native clone, immutable registry, shrinking recovery, debounce save, fresh reopen and older Undo completed"<<std::endl;
}
