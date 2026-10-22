// Separate Code command fault proof. The established four-path rollback summary
// remains Restore/Snippet/Table/Link; this case has its own completion line.
static void NotesCodeRollbackTransactions(const fs::path &artifacts={}) {
  notes_rollback::Scope scope;
  const unsigned failuresBefore=notes_rollback::failures;
  const auto output=artifacts.empty()?g_sDataDir/L"code-fault-evidence":artifacts;
  fs::create_directories(output);
  const std::wstring alpha=L"Code fault Alpha",beta=L"Code fault Beta",url=L"https://example.test/code-original";
  Fixture(alpha);Fixture(beta);const auto alphaRoot=g_sDataDir/L"Sites"/alpha,betaRoot=g_sDataDir/L"Sites"/beta;
  auto seed=notes_rollback::RecoverySeed(url),other=notes_rollback::RecoverySeed(L"https://example.test/code-other-client");
  other.rtf=NotesPlainRtf(L"Portal\r\nOther isolated Code client");
  Check(client_notes::WriteNotebook(alphaRoot,{seed},client_notes::ReadNotebook(alphaRoot)) &&
      client_notes::WriteNotebook(betaRoot,{other},client_notes::ReadNotebook(betaRoot)),
      "Code fault fixtures commit separate clients with colliding local IDs and distinct URLs");
  HWND owner=Open(alpha);auto &state=State(owner);HWND edit=Edit(owner);
  Select(edit,-1,-1);Type(owner,L" earlier saved edit");SendMessageW(edit,EM_STOPGROUPTYPING,0,0);
  Check(notes_rollback::AwaitTimer(owner) && !state.saveFailed,"An earlier valid Code-fixture edit reaches the actual debounce autosave");
  const auto older=notes_rollback::Read(edit);
  Select(edit,-1,-1);Type(owner,L" original unsaved draft");SendMessageW(edit,EM_STOPGROUPTYPING,0,0);
  const auto before=notes_rollback::Read(edit);
  NotesStream rawBefore;EDITSTREAM nativeBefore{reinterpret_cast<DWORD_PTR>(&rawBefore),0,NotesStreamOut};
  SendMessageW(edit,EM_STREAMOUT,SF_RTF,reinterpret_cast<LPARAM>(&nativeBefore));
  while(!rawBefore.bytes.empty() && rawBefore.bytes.back()=='\0')rawBefore.bytes.pop_back();
  const auto registry=state.editorLinks[state.attachedPageId];const bool formatting=state.formattingChanged,metadata=state.metadataChanged;
  CHARRANGE selection{};NotesSelection(edit,selection);POINT scroll{};SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
  const auto alphaBytes=client_notes::ReadNotebook(alphaRoot).notebook_snapshot.bytes,betaBytes=client_notes::ReadNotebook(betaRoot).notebook_snapshot.bytes;
  Check(before.ok && older.ok && !nativeBefore.dwError && !rawBefore.bytes.empty() && SendMessageW(edit,EM_CANUNDO,0,0) &&
      SendMessageW(edit,EM_GETMODIFY,0,0) && NotesNativeLinkAddress(edit,state,{0,6})==url,
      "Code fault begins with a valid unsaved native draft, original URL and independent older Undo image");
  notes_rollback::LateStreamFailure fault;fault.before=before;
  Check(SetWindowSubclass(edit,notes_rollback::FailActualStream,0x434F4445,reinterpret_cast<DWORD_PTR>(&fault))!=FALSE,
      "Code late stream fault is installed only on the private actual editor");
  NotesCodeInput code;code.language=1;code.source=L"Write-Output 'REJECTED \u03a9'\r\nGet-Service";
  const bool inserted=NotesInsertCode(owner,state,code);
  RemoveWindowSubclass(edit,notes_rollback::FailActualStream,0x434F4445);
  const auto immediate=notes_rollback::Read(edit);
  NotesStream rawImmediate;EDITSTREAM nativeImmediate{reinterpret_cast<DWORD_PTR>(&rawImmediate),0,NotesStreamOut};
  SendMessageW(edit,EM_STREAMOUT,SF_RTF,reinterpret_cast<LPARAM>(&nativeImmediate));
  while(!rawImmediate.bytes.empty() && rawImmediate.bytes.back()=='\0')rawImmediate.bytes.pop_back();
  std::cout<<"Code injected stream: observed="<<fault.observed<<" nativeMutation="<<fault.mutated
      <<" bytes="<<fault.delivered<<"/"<<fault.total<<" error="<<fault.error<<" returned="<<inserted<<std::endl;
  Check(fault.observed && fault.mutated && fault.error!=0 && fault.delivered==fault.limit && !inserted,
      "Code failure follows a real late native partial stream instead of preview rejection");
  notes_rollback::Expect(notes_rollback::Same(immediate,before) && !nativeImmediate.dwError && rawImmediate.bytes==rawBefore.bytes &&
      NotesNativeLinkAddress(edit,state,{0,6})==url,"Rejected Code insertion immediately restores the exact original unsaved raw RTF/text and URL");
  CHARRANGE afterSelection{};NotesSelection(edit,afterSelection);POINT afterScroll{};SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&afterScroll));
  notes_rollback::Expect(notes_rollback::SameRegistry(registry,state.editorLinks[state.attachedPageId]) &&
      state.formattingChanged==formatting && state.metadataChanged==metadata && SendMessageW(edit,EM_GETMODIFY,0,0) &&
      afterSelection.cpMin==selection.cpMin && afterSelection.cpMax==selection.cpMax && afterScroll.x==scroll.x && afterScroll.y==scroll.y,
      "Code recovery preserves the complete original URL registry, dirty flags and native selection/scroll");
  notes_rollback::Expect(client_notes::ReadNotebook(alphaRoot).notebook_snapshot.bytes==alphaBytes &&
      client_notes::ReadNotebook(betaRoot).notebook_snapshot.bytes==betaBytes,"Failed Code return leaves both clients' durable notebooks byte-for-byte unchanged");
  const bool offered=SendMessageW(edit,EM_CANREDO,0,0)!=0;const auto redone=SendMessageW(edit,EM_REDO,0,0);
  const auto afterRedo=notes_rollback::Read(edit);const bool timer=notes_rollback::AwaitTimer(owner);
  const bool durableOriginal=notes_rollback::Durable(owner,alphaRoot,before,url);
  const bool otherSafe=client_notes::ReadNotebook(betaRoot).notebook_snapshot.bytes==betaBytes;
  std::cout<<"Code recovery Redo/autosave: offered="<<offered<<" result="<<redone<<" sameOriginal="<<notes_rollback::Same(afterRedo,before)
      <<" actualTimer="<<timer<<" durableOriginal="<<durableOriginal<<" otherClient="<<otherSafe<<std::endl;
  notes_rollback::Expect(!offered && !redone && notes_rollback::Same(afterRedo,before),"Pure Redo cannot publish rejected Code content or replace the original draft");
  notes_rollback::Expect(timer && !state.saveFailed && durableOriginal && otherSafe,
      "Actual autosave after a rejected Code insertion persists only the original draft and preserves the other client");
  Put(output/L"before-raw.rtf",rawBefore.bytes);Put(output/L"native-partial.rtf",fault.partial.rtf);
  Put(output/L"after-failed-return-raw.rtf",rawImmediate.bytes);Put(output/L"after-pure-redo.rtf",afterRedo.rtf);
  Put(output/L"durable-after-timer.ctn",client_notes::ReadNotebook(alphaRoot).notebook_snapshot.bytes);
  SendMessageW(edit,EM_UNDO,0,0);const auto afterOlderUndo=notes_rollback::Read(edit);const bool olderTimer=notes_rollback::AwaitTimer(owner);
  notes_rollback::Expect(notes_rollback::Same(afterOlderUndo,older) && NotesNativeLinkAddress(edit,state,{0,6})==url,
      "One older Undo after rejected Code removes exactly the preceding valid unsaved edit and retains its URL");
  notes_rollback::Expect(olderTimer && notes_rollback::Durable(owner,alphaRoot,older,url) &&
      client_notes::ReadNotebook(betaRoot).notebook_snapshot.bytes==betaBytes,"Older Code-fixture Undo autosaves its independent original image without crossing clients");
  Put(output/L"after-older-undo.rtf",afterOlderUndo.rtf);
  CloseClientNotesDialog(owner,state);NotesBackgroundAudit("Code late stream recovery cleanup");
  const auto failed=notes_rollback::failures-failuresBefore;
  std::cout<<"Code insertion recovery proof: 1 injected path; "<<failed<<" failed expectations."<<std::endl;
  if(failed)throw std::runtime_error("Rejected Code insertion changes the unsaved draft, native history or durable ownership");
}
