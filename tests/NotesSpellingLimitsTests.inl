// Native EM_REPLACESEL can accept a complete correction beyond EM_EXLIMITTEXT.
// Verify the observed complete result, durability and both native Undo steps.
static void SpellingNativeLimitCases(HWND owner){
  auto &state=State(owner);const auto edit=Edit(owner);
  Select(edit,0,-1);Type(owner,L"Original marker. A speling example. Closing sentinel.");Save(owner);
  SendMessageW(edit,EM_STOPGROUPTYPING,0,0);SendMessageW(edit,EM_EMPTYUNDOBUFFER,0,0);
  std::string beforePrior;Check(StreamClientNotesOut(edit,beforePrior),"Native spelling-limit fixture records the document before its older edit");
  Select(edit,LONG_MAX,LONG_MAX);Type(owner,L" Prior edit.");SendMessageW(edit,EM_STOPGROUPTYPING,0,0);Save(owner);
  Check(SendMessageW(edit,EM_CANUNDO,0,0)!=0,"The spelling-limit fixture retains an independently completed older Undo action");
  const auto durable=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes;
  std::string before;Check(StreamClientNotesOut(edit,before),"Native spelling-limit fixture records its exact baseline RTF");
  Fixture(L"Spelling limit isolated");const auto isolated=g_sDataDir/L"Sites"/L"Spelling limit isolated";
  client_notes::NotePage independent{L"Other client",true,NotesPlainRtf(L"Independent client sentinel: leave this unchanged."),{}};independent.id=1;
  Check(client_notes::WriteNotebook(isolated,{independent},client_notes::ReadNotebook(isolated)),"A separately saved client notebook protects the spelling-limit isolation boundary");
  const auto otherBytes=client_notes::ReadNotebook(isolated).notebook_snapshot.bytes;
  Select(edit,2,2);
  auto check=MakeSpellingState(owner);const auto position=check.snapshot.find(L"speling");
  Check(position!=check.snapshot.npos,"The spelling-limit fixture identifies a real eligible misspelled word");
  const auto dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_SPELLING),owner,NotesSpellingProc,reinterpret_cast<LPARAM>(&check));
  Check(dialog&&IsWindow(dialog),"The native spelling-limit dialog is created through the existing hidden-window guards");
  KillTimer(dialog,kNotesSpellingTimer);check.error=notes_spelling::Error{position,7,{}};
  const std::wstring replacement=L"correct spelling",original=check.snapshot;
  const auto originalScope=check.scope;auto expected=original;expected.replace(position,7,replacement);
  SetDlgItemTextW(dialog,IDC_NOTES_SPELL_SUGGESTIONS,replacement.c_str());
  const auto priorLimit=SendMessageW(edit,EM_GETLIMITTEXT,0,0);
  const LONG limited=check.scope.last+2;
  SendMessageW(edit,EM_EXLIMITTEXT,0,limited);
  Check(SendMessageW(edit,EM_GETLIMITTEXT,0,0)==limited&&expected.size()>static_cast<size_t>(limited),"The explicit correction is longer than the real native typing limit");
  const auto accepted=NotesSpellingChange(dialog,check);
  std::string after;Check(StreamClientNotesOut(edit,after),"The native spelling-limit result retains a readable RTF document");
  std::wstring actual;notes_spelling::NativeText(edit,{0,static_cast<LONG>(SendMessageW(edit,WM_GETTEXTLENGTH,0,0))},actual);
  CHARRANGE selected{};NotesSelection(edit,selected);
  std::string actualUtf8;client_notes::EncodeUtf8(actual,actualUtf8);
  std::cout<<"Native spelling limit: original="<<original.size()<<" limit="<<limited<<" corrected="<<expected.size()<<" accepted="<<accepted
      <<" check.changed="<<check.changed<<" RTF-preserved="<<(before==after)<<" actual="<<actualUtf8<<'\n';
  SpellCheck(accepted&&check.changed,"A valid explicit correction accepted in full by native RichEdit succeeds");
  SpellCheck(actual==expected&&check.snapshot==expected&&check.scope.first==originalScope.first&&check.scope.last==static_cast<LONG>(expected.size()),"The longer spelling correction preserves every neighboring character and refreshes its exact native snapshot");
  const auto saved=client_notes::ReadNotebook(state.clientRoot);std::string expectedNotebook;
  const auto savedPage=std::find_if(saved.pages.begin(),saved.pages.end(),[&](const auto &page){return page.id==state.attachedPageId;});
  SpellCheck(saved.status==client_notes::ReadStatus::Ok&&savedPage!=saved.pages.end()&&savedPage->rtf==after&&client_notes::SerializeNotebook(state.pages,expectedNotebook)&&saved.notebook_snapshot.bytes==expectedNotebook&&saved.notebook_snapshot.bytes!=durable,"An accepted longer correction durably saves its exact RTF and complete notebook state");
  SpellCheck(client_notes::ReadNotebook(isolated).notebook_snapshot.bytes==otherBytes,"Saving a longer spelling correction leaves the independent client's exact notebook bytes unchanged");
  SpellCheck(selected.cpMin==static_cast<LONG>(position+replacement.size())&&selected.cpMax==selected.cpMin,"An accepted spelling correction leaves the native caret after its complete replacement");
  SendMessageW(edit,EM_EXLIMITTEXT,0,priorLimit);DestroyWindow(dialog);
  const auto undone=SendMessageW(edit,EM_UNDO,0,0);std::string afterUndo;Check(StreamClientNotesOut(edit,afterUndo),"The native spelling-limit fixture can inspect the correction's Undo result");
  SpellCheck(undone&&afterUndo==before,"One native Undo restores the exact document before the complete spelling correction");
  Save(owner);const auto undoneOlder=SendMessageW(edit,EM_UNDO,0,0);std::string afterOlder;Check(StreamClientNotesOut(edit,afterOlder),"The native spelling-limit fixture can inspect the independent older Undo action after saving");
  SpellCheck(undoneOlder&&afterOlder==beforePrior&&!SendMessageW(edit,EM_CANUNDO,0,0),"The second Undo still removes only the user's older edit, with no extra replacement or save action");
  SpellCheck(client_notes::ReadNotebook(isolated).notebook_snapshot.bytes==otherBytes,"Reviewing the preserved Undo action cannot change the other client's notebook");
  NotesBackgroundAudit("native spelling limit transaction and older Undo");
}
static void SpellingSoftLineCases(HWND owner){
  auto &state=State(owner);const auto edit=Edit(owner);
  for(const bool commandFirst:{false,true}){
    const std::string prose="Ordinary prose spelingreview",command="Get-Printer commandspeling";
    const std::string rtf="{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}\\f0\\fs24 "+
        (commandFirst?command:prose)+"\\line "+(commandFirst?prose:command)+"\\par}";
    const bool loading=state.loading;state.loading=true;NotesStream source{rtf};EDITSTREAM input{reinterpret_cast<DWORD_PTR>(&source),0,NotesStreamIn};
    SendMessageW(edit,EM_STREAMIN,SF_RTF,reinterpret_cast<LPARAM>(&input));state.loading=loading;
    Check(!input.dwError&&source.offset==source.bytes.size(),"The independent spelling fixture loads an actual native RTF soft line");
    notes_spelling::ComPtr<ITextDocument> document;Check(SUCCEEDED(notes_spelling::TextDocument(edit,document)),"The soft-line spelling fixture obtains its real native text model");
    notes_spelling::ComPtr<ITextRange> range;LONG story=0;Check(SUCCEEDED(document->Range(0,0,&range))&&SUCCEEDED(range->GetStoryLength(&story))&&story>1,"The soft-line spelling fixture has a bounded native story");
    const notes_spelling::Range scope{0,story-1};std::wstring text,masked;
    Select(edit,1,2);CHARRANGE selected{};NotesSelection(edit,selected);const auto undo=SendMessageW(edit,EM_CANUNDO,0,0);
    std::string before;Check(StreamClientNotesOut(edit,before),"The soft-line spelling fixture captures exact native RTF before read-only inspection");
    Check(SUCCEEDED(notes_spelling::ReadScope(edit,scope,text,masked)),"Actual native soft-line text remains readable with exact spelling offsets");
    Check(std::count(text.begin(),text.end(),L'\v')==1&&text.find(L"\\line")==text.npos,"Native RTF line syntax becomes one real U+000B soft break");
    Check(notes_spelling::CommandLine(L"Get-Printer commandspeling")&&!notes_spelling::CommandLine(L"Ordinary prose spelingreview"),"The command and prose are independently classified before testing their shared native paragraph");
    std::cout<<"Soft-line spelling: command-first="<<commandFirst<<" command-masked="<<(masked.find(L"commandspeling")==masked.npos)
        <<" prose-retained="<<(masked.find(L"spelingreview")!=masked.npos)<<" native-soft-breaks="<<std::count(text.begin(),text.end(),L'\v')<<'\n';
    SpellCheck(masked.size()==text.size(),"Soft-line spelling masks retain every exact native UTF-16 offset");
    SpellCheck(masked.find(L"commandspeling")==masked.npos,"A native soft break does not expose a command argument to spelling review");
    SpellCheck(masked.find(L"spelingreview")!=masked.npos,"A command separated by a native soft break does not hide neighboring prose from spelling review");
    std::string after;Check(StreamClientNotesOut(edit,after),"The soft-line spelling result retains readable native rich text");CHARRANGE current{};NotesSelection(edit,current);
    SpellCheck(before==after&&current.cpMin==selected.cpMin&&current.cpMax==selected.cpMax&&SendMessageW(edit,EM_CANUNDO,0,0)==undo,"Inspecting command and prose soft lines preserves exact RTF, selection and Undo availability");
  }
  NotesBackgroundAudit("native spelling soft-line command boundaries");
}
