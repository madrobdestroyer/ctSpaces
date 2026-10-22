// A native fault accepts only part of the requested explicit correction. The
// real spelling UI must reject it transactionally before marking or saving.
struct SpellingPartialFault {
 ClientNotesDialogState *state=nullptr;CHARRANGE target{};
 std::wstring replacement,partial;unsigned attempted=0;
 bool targetObserved=false,partialObserved=false,loadingObserved=false;
};
static LRESULT CALLBACK SpellingPartialSubclass(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
 auto &fault=*reinterpret_cast<SpellingPartialFault *>(data);
 if(message==EM_REPLACESEL&&w&&l&&!fault.attempted&&fault.replacement==reinterpret_cast<const wchar_t *>(l)){
  ++fault.attempted;CHARRANGE selected{};SendMessageW(window,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selected));
  fault.targetObserved=selected.cpMin==fault.target.cpMin&&selected.cpMax==fault.target.cpMax;
  fault.loadingObserved=fault.state&&fault.state->loading;
  const auto result=DefSubclassProc(window,message,w,reinterpret_cast<LPARAM>(fault.partial.c_str()));
  std::wstring actual;fault.partialObserved=SUCCEEDED(notes_spelling::NativeText(window,{selected.cpMin,selected.cpMin+static_cast<LONG>(fault.partial.size())},actual))&&actual==fault.partial;
  return result;
 }
 return DefSubclassProc(window,message,w,l);
}
static void SpellingPartialCorrectionCases(){
 Fixture(L"Spelling partial source");const auto owner=Open(L"Spelling partial source");auto &state=State(owner);const auto edit=Edit(owner);
 Select(edit,0,-1);Type(owner,L"Original marker. A speling example. Closing sentinel.");Save(owner);
 SendMessageW(edit,EM_STOPGROUPTYPING,0,0);SendMessageW(edit,EM_EMPTYUNDOBUFFER,0,0);
 notes_undo::Document beforePrior;Check(notes_undo::CaptureDocument(edit,beforePrior),"The partial-correction fixture records its exact raw native document before an older user edit");
 Select(edit,LONG_MAX,LONG_MAX);Type(owner,L" Prior edit.");SendMessageW(edit,EM_STOPGROUPTYPING,0,0);Save(owner);
 const auto durable=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes;
 std::string storedBefore;Check(client_notes::SerializeNotebook(state.pages,storedBefore),"The partial-correction fixture records its complete stored notebook state");
 Fixture(L"Spelling partial isolated");const auto isolated=g_sDataDir/L"Sites"/L"Spelling partial isolated";
 client_notes::NotePage independent{L"Independent",true,NotesPlainRtf(L"Independent client sentinel: leave this unchanged."),{}};independent.id=1;
 Check(client_notes::WriteNotebook(isolated,{independent},client_notes::ReadNotebook(isolated)),"The partial-correction fixture independently saves a second client's notebook");
 const auto otherBytes=client_notes::ReadNotebook(isolated).notebook_snapshot.bytes;
 Select(edit,2,2);auto check=MakeSpellingState(owner);const auto position=check.snapshot.find(L"speling");
 Check(position!=check.snapshot.npos,"The partial-correction fixture has an exact eligible prose word");
 const auto dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_SPELLING),owner,NotesSpellingProc,reinterpret_cast<LPARAM>(&check));
 Check(dialog&&IsWindow(dialog),"The partial-correction proof uses the actual guarded spelling dialog");KillTimer(dialog,kNotesSpellingTimer);
 check.error=notes_spelling::Error{position,7,{}};SetDlgItemTextW(dialog,IDC_NOTES_SPELL_SUGGESTIONS,L"spelling");
 const auto snapshot=check.snapshot;const auto scope=check.scope;const auto scan=check.scan;
 notes_undo::Document before;notes_undo::View view;Check(notes_undo::CaptureDocument(edit,before)&&notes_undo::CaptureView(edit,view),"The partial-correction proof captures exact raw RTF, native bindings and input view");
 const auto canUndo=SendMessageW(edit,EM_CANUNDO,0,0),canRedo=SendMessageW(edit,EM_CANREDO,0,0);
 const auto formatting=state.formattingChanged,metadata=state.metadataChanged,saveFailed=state.saveFailed,loading=state.loading;
 const auto events=SendMessageW(edit,EM_GETEVENTMASK,0,0);
 SpellingPartialFault fault{&state,{scope.first+static_cast<LONG>(position),scope.first+static_cast<LONG>(position+7)},L"spelling",L"spe"};
 Check(SetWindowSubclass(edit,SpellingPartialSubclass,0x535050, reinterpret_cast<DWORD_PTR>(&fault))!=FALSE,"The private native partial-correction fault installs without external input");
 const bool accepted=NotesSpellingChange(dialog,check);RemoveWindowSubclass(edit,SpellingPartialSubclass,0x535050);
 notes_undo::Document after;const bool readable=notes_undo::CaptureDocument(edit,after);
 const auto afterDisk=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes;
 std::string storedAfter;const bool serialized=client_notes::SerializeNotebook(state.pages,storedAfter);
 CHARRANGE selected{};POINT scroll{};SendMessageW(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selected));SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
 const auto diagnostic=g_sDataDir/L"spelling-partial-diagnostics";Put(diagnostic/L"before.rtf",before.rtf);Put(diagnostic/L"after.rtf",after.rtf);Put(diagnostic/L"durable-before.ctn",durable);Put(diagnostic/L"durable-after.ctn",afterDisk);
 std::cout<<"Actual spelling partial-correction: attempted="<<fault.attempted<<"; exactTarget="<<fault.targetObserved<<"; partialNativeObserved="<<fault.partialObserved<<"; loading="<<fault.loadingObserved<<"; accepted="<<accepted<<"; checkChanged="<<check.changed<<"; rawNativeExact="<<(readable&&after.rtf==before.rtf)<<"; nativeBindingsExact="<<(readable&&after.links==before.links)<<"; durableExact="<<(afterDisk==durable)<<"; storedExact="<<(serialized&&storedAfter==storedBefore)<<"; canUndo="<<SendMessageW(edit,EM_CANUNDO,0,0)<<"; canRedo="<<SendMessageW(edit,EM_CANREDO,0,0)<<std::endl;
 std::wcout<<L"Actual spelling partial-correction diagnostics: "<<diagnostic.wstring()<<std::endl;
 SpellCheck(fault.attempted==1&&fault.targetObserved&&fault.partialObserved&&fault.loadingObserved,"The actual spelling UI reaches the exact word and observes a real partial native insertion under loading suppression");
 SpellCheck(!accepted&&!check.changed,"A refused partial spelling correction is never reported as an accepted change");
 SpellCheck(readable&&after.length==before.length&&after.rtf==before.rtf&&after.links==before.links,"A refused partial spelling correction restores exact raw native content and binding runs");
 SpellCheck(afterDisk==durable&&serialized&&storedAfter==storedBefore,"The actual spelling failure cannot publish partial text to stored pages or durable notebook bytes");
 SpellCheck(check.snapshot==snapshot&&check.scope.first==scope.first&&check.scope.last==scope.last&&check.scan==scan&&check.error&&check.error->first==position,"Refused spelling leaves its original dictionary snapshot, scope and pending error intact");
 SpellCheck(state.formattingChanged==formatting&&state.metadataChanged==metadata&&state.saveFailed==saveFailed&&state.loading==loading&&!state.pendingTransactionRecovery&&SendMessageW(edit,EM_GETEVENTMASK,0,0)==events,"Recovered spelling preserves dirty/save/loading flags, event routing and safe editor ownership");
 SpellCheck(selected.cpMin==view.selection.cpMin&&selected.cpMax==view.selection.cpMax&&scroll.x==view.scroll.x&&scroll.y==view.scroll.y&&SendMessageW(edit,EM_GETLIMITTEXT,0,0)==view.limit&&SendMessageW(edit,EM_GETAUTOURLDETECT,0,0)==view.autoUrlFlags&&static_cast<BOOL>(SendMessageW(edit,EM_GETMODIFY,0,0))==view.modified,"Refused spelling preserves original native selection, scroll, limits, auto-detection and modified state");
 SpellCheck(canUndo!=0&&SendMessageW(edit,EM_CANUNDO,0,0)==canUndo&&!canRedo&&!SendMessageW(edit,EM_CANREDO,0,0),"Refused spelling keeps the older user's Undo and cannot leave its partial insertion in Redo");
 SendMessageW(edit,EM_REDO,0,0);SpellCheck(notes_undo::VerifyOriginal(edit,before),"Pure Redo after the refused spelling correction cannot resurrect its partial insertion");
 const bool undone=SendMessageW(edit,EM_UNDO,0,0)!=FALSE;SpellCheck(undone&&notes_undo::VerifyOriginal(edit,beforePrior),"One Undo after refused spelling still removes only the user's older completed edit");
 const bool redone=SendMessageW(edit,EM_REDO,0,0)!=FALSE;SpellCheck(redone&&notes_undo::VerifyOriginal(edit,before),"Redo after the older spelling-fixture Undo restores only the original valid user edit");
 SpellCheck(client_notes::ReadNotebook(isolated).notebook_snapshot.bytes==otherBytes,"Refused spelling and native history checks leave the independent client's bytes unchanged");
 DestroyWindow(dialog);DestroyWindow(owner);NotesBackgroundAudit("actual spelling partial correction, persistence and older Undo");
}
static void SpellingFriendlyLinkSuccessCases(){
 const std::wstring name=L"Spelling friendly-link success",address=L"https://example.test/spelling-client-ticket?client=private";
 Fixture(name);auto owner=Open(name);auto &state=State(owner);auto edit=Edit(owner);
 Type(owner,L"Portal\r\nThe speling needs review.");Select(edit,0,6);inputs.push_back({L"Portal",address});Command(owner,IDC_NOTES_LINK);Save(owner);
 Check(NotesNativeLinkAddress(edit,state,{0,6})==address,"The actual spelling-success fixture begins with an unrelated registered friendly link");
 SendMessageW(edit,EM_STOPGROUPTYPING,0,0);SendMessageW(edit,EM_EMPTYUNDOBUFFER,0,0);
 const auto root=state.clientRoot;const auto page=state.attachedPageId;const auto durable=client_notes::ReadNotebook(root).notebook_snapshot.bytes;
 Fixture(L"Spelling friendly-link isolated");const auto isolated=g_sDataDir/L"Sites"/L"Spelling friendly-link isolated";
 client_notes::NotePage independent{L"Independent",true,NotesPlainRtf(L"Independent friendly-link test client."),{}};independent.id=1;
 Check(client_notes::WriteNotebook(isolated,{independent},client_notes::ReadNotebook(isolated)),"The friendly-link spelling test independently saves another client's notebook");
 const auto otherBytes=client_notes::ReadNotebook(isolated).notebook_snapshot.bytes;
 Select(edit,9,9);auto check=MakeSpellingState(owner);const auto position=check.snapshot.find(L"speling");
 Check(position!=check.snapshot.npos&&check.masked.substr(position,7)==L"speling","The spelling success target is eligible prose outside the unrelated registered link");
 notes_undo::Document before;Check(notes_undo::CaptureDocument(edit,before),"The friendly-link spelling fixture records its original raw native document");
 auto expected=check.snapshot;expected.replace(position,7,L"spelling");
 const auto dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_SPELLING),owner,NotesSpellingProc,reinterpret_cast<LPARAM>(&check));
 Check(dialog&&IsWindow(dialog),"The friendly-link success proof uses the real guarded spelling dialog");KillTimer(dialog,kNotesSpellingTimer);
 check.error=notes_spelling::Error{position,7,{}};SetDlgItemTextW(dialog,IDC_NOTES_SPELL_SUGGESTIONS,L"spelling");
 const bool accepted=NotesSpellingChange(dialog,check);notes_undo::Document after;const bool readable=notes_undo::CaptureDocument(edit,after);
 const auto nativeAddress=NotesNativeLinkAddress(edit,state,{0,6});const auto saved=client_notes::ReadNotebook(root);
 const auto savedPage=std::find_if(saved.pages.begin(),saved.pages.end(),[&](const auto &entry){return entry.id==page;});
 const auto matchingLink=[&](const client_notes::NotePage &entry){return entry.links==std::vector<std::pair<std::wstring,std::wstring>>{{L"Portal",address}}&&entry.linkRangesKnown&&entry.linkRanges==std::vector<client_notes::LinkRange>{{0,6,0}};};
 std::wstring actual;const bool textRead=SUCCEEDED(notes_spelling::NativeText(edit,{0,notes_undo::Length(edit)},actual));
 std::cout<<"Actual spelling unrelated-link success: accepted="<<accepted<<"; changed="<<check.changed<<"; expectedText="<<(textRead&&actual==expected)<<"; nativeURL="<<(nativeAddress==address)<<"; savedURL="<<(savedPage!=saved.pages.end()&&matchingLink(*savedPage))<<"; savedChanged="<<(saved.notebook_snapshot.bytes!=durable)<<std::endl;
 const auto diagnostic=g_sDataDir/L"spelling-friendly-link-diagnostics";Put(diagnostic/L"before.rtf",before.rtf);Put(diagnostic/L"after.rtf",after.rtf);Put(diagnostic/L"durable-before.ctn",durable);Put(diagnostic/L"durable-after.ctn",saved.notebook_snapshot.bytes);
 SpellCheck(accepted&&check.changed&&textRead&&actual==expected&&check.snapshot==expected,"An actual full spelling correction succeeds without changing neighboring prose or link text");
 SpellCheck(readable&&nativeAddress==address,"Successful spelling retains the unrelated friendly link's real native binding and URL");
 SpellCheck(saved.status==client_notes::ReadStatus::Ok&&savedPage!=saved.pages.end()&&matchingLink(*savedPage)&&saved.notebook_snapshot.bytes!=durable,"Successful spelling saves the corrected word and the unrelated exact label, range and target mapping");
 SpellCheck(client_notes::ReadNotebook(isolated).notebook_snapshot.bytes==otherBytes,"Saving the full spelling correction cannot change another client's notebook");
 DestroyWindow(dialog);Command(owner,IDC_NOTES_UNDO);
 SpellCheck(notes_undo::VerifyOriginal(edit,before)&&NotesNativeLinkAddress(edit,state,{0,6})==address&&!SendMessageW(edit,EM_CANUNDO,0,0),"One Undo of actual spelling restores the complete original word and unrelated link in one action");
 Command(owner,IDC_NOTES_REDO);
 SpellCheck(readable&&notes_undo::VerifyOriginal(edit,after)&&NotesNativeLinkAddress(edit,state,{0,6})==address,"One Redo of actual spelling restores the corrected word and unrelated URL together");
 Save(owner);const auto finalBytes=client_notes::ReadNotebook(root).notebook_snapshot.bytes;DestroyWindow(owner);owner=Open(name);auto &reopened=State(owner);edit=Edit(owner);
 std::wstring reopenedText;const bool reopenedRead=SUCCEEDED(notes_spelling::NativeText(edit,{0,notes_undo::Length(edit)},reopenedText));
 SpellCheck(reopenedRead&&reopenedText==expected&&NotesNativeLinkAddress(edit,reopened,{0,6})==address,"A fresh client-note reopen retains the corrected prose and unrelated friendly-link destination");
 SpellCheck(client_notes::ReadNotebook(root).notebook_snapshot.bytes==finalBytes&&client_notes::ReadNotebook(isolated).notebook_snapshot.bytes==otherBytes,"Fresh reopening the successful correction preserves both clients' exact saved notebook bytes");
 DestroyWindow(owner);NotesBackgroundAudit("actual spelling unrelated link success, Undo, save and fresh reopen");
}
