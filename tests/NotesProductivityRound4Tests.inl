// Round4 probes for valid titles whose shortening crosses UTF-16 boundaries.
static void NotesProductivityRound4(bool templatesOnly=false) {
  Fixture(L"Unicode naming round4 QA");HWND owner=Open(L"Unicode naming round4 QA");auto &state=State(owner);
  const std::wstring noteTitle=std::wstring(47,L'N')+L"\xD83D\xDE80"+L" launch";
  Check(client_notes::ValidPageName(noteTitle),"Source note title containing a supplementary Unicode character is valid");
  Type(owner,L"Original note content must remain associated with its title");Save(owner);
  state.pages[state.activePage].name=noteTitle;state.metadataChanged=true;Save(owner);
  const auto original=state.pages[state.activePage];Command(owner,IDC_NOTES_DUPLICATE);
  Check(!state.saveFailed && state.pages.size()==2 && NotesPlain(state.pages[state.activePage])==NotesPlain(original),"Duplicating the Unicode-titled note preserves saved text");
  std::wcout<<L"Generated note name: "<<state.pages[state.activePage].name<<std::endl;
  if(!templatesOnly) Check(state.pages[state.activePage].name!=L"Note" && state.pages[state.activePage].name.starts_with(std::wstring(47,L'N')),
      "Duplicate title shortening preserves the meaningful name instead of falling back to Note at a surrogate boundary");
  NotesTemplates library{owner,&state};Check(NotesLoadTemplates(library),"Shared template library can be loaded for Unicode collision fixture");
  const std::wstring templateTitle=std::wstring(53,L'T')+L"\xD83D\xDE80"+L" starter";
  Check(client_notes::ValidPageName(templateTitle),"Source reusable template title is valid before collision suffixing");
  client_notes::NotePage first{templateTitle,true,NotesPlainRtf(L"Reusable Unicode template body"),{}};first.id=100;
  Check(NotesSaveTemplates(owner,library,{first}),"Valid long Unicode template can be saved unchanged");
  auto next=first;next.id=101;next.name=NotesNewTemplateName(library,templateTitle);
  std::wcout<<L"Generated template name length="<<next.name.size()<<L", valid="<<client_notes::ValidPageName(next.name)<<std::endl;
  Check(client_notes::ValidPageName(next.name) && next.name.starts_with(std::wstring(53,L'T')),
      "Collision suffixing preserves a valid meaningful Unicode template title");
  Check(NotesSaveTemplates(owner,library,{first,next}),"Both Unicode templates save as distinct reusable entries");
  const auto saved=client_notes::ReadNotebook(library.root);
  Check(saved.status==client_notes::ReadStatus::Ok && saved.pages.size()==2 && saved.pages[1].rtf==first.rtf,
      "Unicode generated titles round trip with unchanged reusable content");
  ClientNotesDialogState naming;
  const auto fits=std::wstring(46,L'F')+L"\xD83D\xDE80";
  Check(NotesUniqueName(naming,fits)==fits,"A supplementary title character fitting entirely within the limit stays intact");
  const auto spaced=std::wstring(47,L'S')+L" suffix";
  Check(NotesUniqueName(naming,spaced)==std::wstring(47,L'S'),"A valid long title ending its shortened prefix in whitespace keeps its meaningful name");
  naming.pages.push_back({std::wstring(47,L'N'),true,NotesPlainRtf(L"Independent saved note"),{}});
  const auto collided=NotesUniqueName(naming,noteTitle);
  Check(client_notes::ValidPageName(collided) && collided!=naming.pages.front().name && collided.starts_with(std::wstring(47,L'N')),
      "Shortened supplementary title remains valid and unique when its prefix already exists");
  NotesBackgroundAudit("productivity round4 cleanup");CloseClientNotesDialog(owner,state);
}
