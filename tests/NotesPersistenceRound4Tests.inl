// Round 4 targets authority after legacy-to-notebook migration. These scenarios
// use real files and native editors behind the process-private background guard.
struct NotesRound4DeleteBoundary {HWND owner=nullptr;bool armed=true,wrote=false;HANDLE lock=INVALID_HANDLE_VALUE;};
static LRESULT CALLBACK NotesRound4DeleteWriter(HWND edit,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
  auto &boundary=*reinterpret_cast<NotesRound4DeleteBoundary *>(data);
  if(message==WM_NCDESTROY) RemoveWindowSubclass(edit,NotesRound4DeleteWriter,id);
  if(message==EM_SETMODIFY && !w && boundary.armed) {
    boundary.armed=false;auto &state=State(boundary.owner);const auto snapshot=client_notes::ReadNotebook(state.clientRoot);auto pages=snapshot.pages;
    pages[state.activePage].rtf=NotesPlainRtf(L"A newer external saved version");
    boundary.wrote=client_notes::WriteNotebook(state.clientRoot,pages,snapshot);
    boundary.lock=CreateFileW((state.clientRoot/client_notes::kNotebookFileName).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  }
  return DefSubclassProc(edit,message,w,l);
}
static void NotesRound4DeletedRecovery() {
  notes_persistence::FixtureScope scope;Fixture(L"Round4 deleted draft");HWND owner=Open(L"Round4 deleted draft");auto &state=State(owner);
  Type(owner,L"Previously saved original");Save(owner);Select(Edit(owner),0,-1);Type(owner,L"Unique edited work moved into Trash");
  const auto root=state.clientRoot;const auto recovery=state.draftName;
  NotesRound4DeleteBoundary boundary{owner};SetWindowSubclass(Edit(owner),NotesRound4DeleteWriter,0x4e344445,reinterpret_cast<DWORD_PTR>(&boundary));Command(owner,IDC_NOTES_DELETE);
  Check(boundary.wrote && boundary.lock!=INVALID_HANDLE_VALUE && !boundary.armed,"A real competing writer and CTN lock arrive after Delete's first successful draft save");
  Check(state.saveFailed && std::any_of(state.pages.begin(),state.pages.end(),[](const auto &page){return page.deleted && NotesPlain(page)==L"Unique edited work moved into Trash";}),"Delete's second save fails while its edited Trash draft remains in memory");
  const auto draft=client_notes::ReadFileSnapshot(root,recovery.c_str(),client_notes::kMaxNotebookBytes);std::vector<client_notes::NotePage> draftPages;
  Check(draft.status==client_notes::ReadStatus::Ok && client_notes::ParseNotebook(draft.bytes,draftPages) && std::any_of(draftPages.begin(),draftPages.end(),[](const auto &page){return page.deleted && NotesPlain(page)==L"Unique edited work moved into Trash";}),"The actual crash recovery file contains the unique edited deleted-note content");
  CloseHandle(boundary.lock);DestroyWindow(owner);
  const auto orphan=L"ctSpaces-notes-draft-4294967294.ctn";Check(MoveFileExW((root/recovery).c_str(),(root/orphan).c_str(),0)!=FALSE,"The disposable recovery fixture represents an exited process without changing its bytes");
  owner=Open(L"Round4 deleted draft");auto &reopened=State(owner);
  const auto externalId=reopened.attachedPageId;const auto externalText=NotesPlain(reopened.pages[reopened.activePage]);
  const auto externalNativeRtf=reopened.baselines[reopened.activePage];
  Check(externalText.find(L"A newer external saved version")==0,"Recovery starts from the actual competing writer's native decoded saved text");
  answers.push_back(IDYES);NotesRecoverDrafts(owner,reopened);
  const auto saved=client_notes::ReadNotebook(root);const bool retained=std::any_of(saved.pages.begin(),saved.pages.end(),[](const auto &page){return NotesPlain(page)==L"Unique edited work moved into Trash";});
  std::cout<<"Round4 deleted recovery: unique edit durably retained="<<retained<<", old recovery still exists="<<fs::exists(root/orphan)<<", saved note count="<<saved.pages.size()<<std::endl;
  NotesBackgroundAudit("round4 deleted-note crash recovery");
  Check(retained,"Accepted crash recovery cannot discard a changed deleted note and remove its only unique-content recovery file");
  Check(std::any_of(saved.pages.begin(),saved.pages.end(),[](const auto &page){return page.deleted && !page.open && NotesPlain(page)==L"Unique edited work moved into Trash";}) && std::any_of(saved.pages.begin(),saved.pages.end(),[&](const auto &page){return page.id==externalId && !page.deleted && page.rtf==externalNativeRtf;}),"Recovered unique edited work remains a separate Trash copy alongside the exact native RTF of the intact newer saved note");
  DestroyWindow(owner);
}
static void NotesRound4RecoveryRefusal() {
  notes_persistence::FixtureScope scope;
  for(bool archived:{false,true}) {
    const auto name=archived ? L"Round4 blocked archive recovery" : L"Round4 blocked Trash recovery";Fixture(name);HWND owner=Open(name);auto &state=State(owner);
    Type(owner,L"Saved protected original");Save(owner);const auto root=state.clientRoot;const auto baseline=client_notes::ReadNotebook(root);auto drafts=state.pages;
    auto &page=drafts[state.activePage];page.rtf=NotesPlainRtf(L"Unique failed-recovery content");page.deleted=!archived;page.archived=archived;page.open=page.active=false;
    std::string bytes;Check(client_notes::SerializeNotebook(drafts,bytes) && client_notes::WriteAuxiliary(root,L"ctSpaces-notes-draft-4294967294.ctn",bytes),"A changed Archive or Trash crash draft is held in its actual recovery file");
    HANDLE lock=CreateFileW((root/client_notes::kNotebookFileName).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Check(lock!=INVALID_HANDLE_VALUE,"A native notebook lock prevents the recovered-copy commit");
    answers.push_back(IDYES);NotesRecoverDrafts(owner,state);
    Check(state.saveFailed && client_notes::ReadNotebook(root).notebook_snapshot.bytes==baseline.notebook_snapshot.bytes && client_notes::ReadFileSnapshot(root,L"ctSpaces-notes-draft-4294967294.ctn",client_notes::kMaxNotebookBytes).bytes==bytes,"A failed recovered-copy save retains exact crash draft and prior notebook bytes");
    Check(std::any_of(state.pages.begin(),state.pages.end(),[&](const auto &copy){return copy.id!=page.id && copy.deleted==!archived && copy.archived==archived && !copy.open && NotesPlain(copy)==L"Unique failed-recovery content";}),"Failed recovery keeps its separate copy with the original Archive or Trash intent");
    CloseHandle(lock);Save(owner);
    const auto saved=client_notes::ReadNotebook(root);Check(std::any_of(saved.pages.begin(),saved.pages.end(),[&](const auto &copy){return copy.deleted==!archived && copy.archived==archived && NotesPlain(copy)==L"Unique failed-recovery content";}),"Unlocking and retrying durably saves the retained Archive or Trash draft");
    NotesBackgroundAudit("round4 blocked recovered membership save");DestroyWindow(owner);
  }
  Fixture(L"Round4 full recovery");const auto root=g_sDataDir/L"Sites"/L"Round4 full recovery";const auto empty=client_notes::ReadNotebook(root);std::vector<client_notes::NotePage> pages;
  for(size_t i=0;i<client_notes::kMaxPages;++i) {client_notes::NotePage page{L"Full "+std::to_wstring(i),true,NotesPlainRtf(L"Protected existing content"),{}};page.id=i+1;page.open=page.active=i==0;pages.push_back(std::move(page));}
  Check(client_notes::WriteNotebook(root,pages,empty),"An actual maximum-size destination notebook is committed for recovery refusal");
  auto draft=pages.front();draft.rtf=NotesPlainRtf(L"Unique full-destination Trash work");draft.deleted=true;draft.open=draft.active=false;std::string bytes;
  Check(client_notes::SerializeNotebook({draft},bytes) && client_notes::WriteAuxiliary(root,L"ctSpaces-notes-draft-4294967294.ctn",bytes),"A full destination's unique Trash recovery data is persisted independently");
  HWND owner=Open(L"Round4 full recovery");auto &state=State(owner);const auto baseline=state.original.notebook_snapshot;answers.push_back(IDYES);NotesRecoverDrafts(owner,state);
  Check(state.pages.size()==client_notes::kMaxPages && client_notes::ReadNotebook(root).notebook_snapshot.bytes==baseline.bytes && client_notes::ReadFileSnapshot(root,L"ctSpaces-notes-draft-4294967294.ctn",client_notes::kMaxNotebookBytes).bytes==bytes,"A full notebook cannot consume or delete any unrecovered unique Trash data");
  NotesBackgroundAudit("round4 full destination recovery refusal");DestroyWindow(owner);
}
static void NotesRound4PidCollision() {
  notes_persistence::FixtureScope scope;
  const std::wstring name=L"Round4 reused process ID";Fixture(name);HWND owner=Open(name);auto &initial=State(owner);
  Type(owner,L"Already saved notebook content");Save(owner);const auto root=initial.clientRoot;
  auto oldDraft=initial.pages;oldDraft[initial.activePage].rtf=NotesPlainRtf(L"Unique earlier-session unsaved work");
  oldDraft[initial.activePage].pristine=false;std::string oldBytes;
  Check(client_notes::SerializeNotebook(oldDraft,oldBytes),"An earlier-session recovery notebook serializes");
  const auto collision=L"ctSpaces-notes-draft-"+std::to_wstring(GetCurrentProcessId())+L".ctn";
  DestroyWindow(owner);
  Check(client_notes::WriteAuxiliary(root,collision,oldBytes),"A real earlier recovery file occupies the current reused PID name");
  owner=Open(name);auto &state=State(owner);answers.push_back(IDYES);NotesRecoverDrafts(owner,state);answers.clear();
  const bool recovered=std::any_of(state.pages.begin(),state.pages.end(),[](const auto &page){return NotesPlain(page)==L"Unique earlier-session unsaved work";});
  const auto beforeRecovery=client_notes::ReadFileSnapshot(root,collision.c_str(),client_notes::kMaxNotebookBytes);
  Select(Edit(owner),0,-1);Type(owner,L"This session's different unsaved work");NotesWriteRecovery(owner,state);
  const auto afterRecovery=client_notes::ReadFileSnapshot(root,collision.c_str(),client_notes::kMaxNotebookBytes);
  const bool oldDraftRetained=recovered || (afterRecovery.status==client_notes::ReadStatus::Ok && afterRecovery.bytes==oldBytes);
  std::cout<<"Round4 reused PID: old offered/recovered="<<recovered<<", still existed before new recovery="<<(beforeRecovery.status==client_notes::ReadStatus::Ok)<<", exact old recovery bytes retained="<<(afterRecovery.status==client_notes::ReadStatus::Ok && afterRecovery.bytes==oldBytes)<<", earlier work recoverable="<<oldDraftRetained<<std::endl;
  NotesBackgroundAudit("round4 reused PID recovery");DestroyWindow(owner);
  Check(recovered && oldDraftRetained,"A reused Windows process ID cannot skip or overwrite an earlier session's unsaved recovery file");
}
static void NotesRound4RecoveryLifetime() {
  notes_persistence::FixtureScope scope;
  const auto first=NotesNewDraftName(),second=NotesNewDraftName();DWORD pid=0;uint64_t created=0;
  Check(first!=second && NotesDraftOwner(first,pid,created) && pid==GetCurrentProcessId() && created==NotesProcessCreation(GetCurrentProcess()),"Recovery names distinguish sessions and encode the actual owning process lifetime");
  DWORD ignoredPid=0;uint64_t ignoredTime=0;
  Check(NotesDraftOwner(L"ctSpaces-notes-draft-4294967294.ctn",ignoredPid,ignoredTime) && ignoredPid==4294967294 && !ignoredTime,"Existing PID-only recovery filenames remain readable");
  Check(!NotesDraftOwner(L"ctSpaces-notes-draft-4294967296.ctn",ignoredPid,ignoredTime) && !NotesDraftOwner(L"ctSpaces-notes-draft-1-18446744073709551616-{00000000-0000-0000-0000-000000000000}.ctn",ignoredPid,ignoredTime),"Malformed overflowing owner identities are refused without touching their files");
  Fixture(L"Round4 process lifetime");HWND owner=Open(L"Round4 process lifetime");auto &state=State(owner);
  Type(owner,L"Saved lifetime baseline");Save(owner);const auto root=state.clientRoot;
  auto pages=state.pages;pages[state.activePage].rtf=NotesPlainRtf(L"Previous lifetime's unique work");std::string bytes;
  Check(client_notes::SerializeNotebook(pages,bytes),"Process-lifetime recovery fixture serializes valid notes");
  Check(client_notes::WriteAuxiliary(root,first,bytes),"Another live recovery session is represented by its real process creation identity");
  const auto count=state.pages.size();answers.push_back(IDYES);NotesRecoverDrafts(owner,state);
  Check(state.pages.size()==count && answers.size()==1 && client_notes::ReadFileSnapshot(root,first.c_str(),client_notes::kMaxNotebookBytes).bytes==bytes,"Recovery leaves another genuinely live process-lifetime session untouched");answers.clear();
  auto prior=second;const auto currentTime=std::to_wstring(created);const auto offset=prior.find(currentTime);Check(offset!=std::wstring::npos,"Previous-lifetime fixture changes only its process creation token");prior.replace(offset,currentTime.size(),std::to_wstring(created-1));
  Check(client_notes::WriteAuxiliary(root,prior,bytes),"A reused PID carries a different previous process lifetime");
  answers.push_back(IDNO);NotesRecoverDrafts(owner,state);
  Check(state.pages.size()==count && client_notes::ReadFileSnapshot(root,prior.c_str(),client_notes::kMaxNotebookBytes).bytes==bytes,"Declining recovery preserves exact earlier-lifetime draft bytes");
  Select(Edit(owner),0,-1);Type(owner,L"Current lifetime's pending work");NotesWriteRecovery(owner,state);
  Check(state.draftName!=first && state.draftName!=prior && client_notes::ReadFileSnapshot(root,prior.c_str(),client_notes::kMaxNotebookBytes).bytes==bytes && client_notes::ReadFileSnapshot(root,first.c_str(),client_notes::kMaxNotebookBytes).bytes==bytes,"Current-session recovery cannot overwrite a declined orphan or another live session");Save(owner);
  answers.push_back(IDYES);NotesRecoverDrafts(owner,state);
  const auto saved=client_notes::ReadNotebook(root);
  Check(saved.status==client_notes::ReadStatus::Ok && std::any_of(saved.pages.begin(),saved.pages.end(),[](const auto &page){return NotesPlain(page)==L"Previous lifetime's unique work";}) && !fs::exists(root/prior),"Accepting previous-lifetime recovery durably saves its separate copy before removing only its owned draft");
  Check(client_notes::ReadFileSnapshot(root,first.c_str(),client_notes::kMaxNotebookBytes).bytes==bytes,"Recovered orphan cleanup preserves the other live session's exact recovery file");
  NotesBackgroundAudit("round4 recovery lifetime and cancellation");DestroyWindow(owner);
}
static void NotesPersistenceRound4(bool pidCollision=false) {
  if(pidCollision) {NotesRound4PidCollision();NotesRound4RecoveryLifetime();return;}
  notes_persistence::FixtureScope scope;
  bool readFailures=false,saveFailures=false;
  for(bool rich:{false,true}) {
    const std::wstring name=rich ? L"Round4 authoritative RTF" : L"Round4 authoritative TXT";
    Fixture(name);HWND owner=Open(name);auto &state=State(owner);
    Type(owner,L"Authoritative current notebook");state.pages[state.activePage].name=L"Saved work";state.metadataChanged=true;Save(owner);
    const auto root=state.clientRoot;const auto ctn=client_notes::ReadNotebook(root).notebook_snapshot;
    const auto legacyName=rich ? client_notes::kRichFileName : client_notes::kFileName;
    Put(root/legacyName,rich ? "{\\rtf1 Obsolete fallback}" : "Obsolete fallback");
    const auto withLegacy=client_notes::ReadNotebook(root);
    Check(withLegacy.status==client_notes::ReadStatus::Ok && withLegacy.notebook_snapshot.bytes==ctn.bytes,"A valid notebook stays authoritative over readable legacy fallback content");
    HANDLE legacy=CreateFileW((root/legacyName).c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(legacy!=INVALID_HANDLE_VALUE,"A real exclusive legacy-file reader is held");
    const auto lockedRead=client_notes::ReadNotebook(root);
    const bool readable=lockedRead.status==client_notes::ReadStatus::Ok && lockedRead.notebook_snapshot.bytes==ctn.bytes && !lockedRead.pages.empty();
    std::cout<<"Round4 locked "<<(rich ? "RTF" : "TXT")<<": CTN snapshot status="<<static_cast<int>(lockedRead.notebook_snapshot.status)<<", notebook status="<<static_cast<int>(lockedRead.status)<<", authoritative readable="<<readable<<std::endl;
    readFailures|=!readable;CloseHandle(legacy);
    DestroyWindow(owner);owner=Open(name);auto &current=State(owner);
    Select(Edit(owner),0,-1);Type(owner,L"Our new pending work");const auto draft=NotesWindowText(Edit(owner));
    Put(root/legacyName,rich ? "{\\rtf1 Unrelated legacy update}" : "Unrelated legacy update");
    const bool saved=SaveClientNotesDraft(owner,current,false);
    const auto disk=client_notes::ReadNotebook(root);
    const bool committed=saved && !current.saveFailed && disk.status==client_notes::ReadStatus::Ok && NotesPlain(disk.pages[current.activePage])==draft;
    std::cout<<"Round4 changed "<<(rich ? "RTF" : "TXT")<<": save="<<saved<<", saveFailed="<<current.saveFailed<<", editor retained="<<(NotesWindowText(Edit(owner))==draft)<<", authoritative commit="<<committed<<std::endl;
    saveFailures|=!committed;
    Check(NotesWindowText(Edit(owner))==draft,"An unrelated legacy update does not replace the active native draft");
    NotesBackgroundAudit("round4 legacy authority");DestroyWindow(owner);
  }
  Check(!readFailures,"A valid authoritative CTN opens even when an obsolete TXT or RTF fallback is exclusively locked");
  Check(!saveFailures,"An unrelated obsolete TXT or RTF update cannot block saving an existing authoritative CTN");
  for(bool rich:{false,true}) {
    const auto name=rich ? L"Round4 first RTF migration" : L"Round4 first TXT migration";Fixture(name);const auto root=g_sDataDir/L"Sites"/name;
    const auto legacyName=rich ? client_notes::kRichFileName : client_notes::kFileName;
    Put(root/legacyName,rich ? "{\\rtf1 First migration source}" : "First migration source");const auto baseline=client_notes::ReadNotebook(root);
    Check(baseline.status==client_notes::ReadStatus::Ok && baseline.notebook_snapshot.status==client_notes::ReadStatus::Missing,"A missing CTN still reads its existing legacy migration source");
    auto pages=baseline.pages;pages.front().rich=true;pages.front().text.clear();pages.front().rtf=NotesPlainRtf(L"Migration edit");
    HANDLE legacy=CreateFileW((root/legacyName).c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(legacy!=INVALID_HANDLE_VALUE,"First migration has a real exclusive legacy source lock");
    const bool blocked=client_notes::ReadNotebook(root).status==client_notes::ReadStatus::Error && !client_notes::WriteNotebook(root,pages,baseline) && !fs::exists(root/client_notes::kNotebookFileName);CloseHandle(legacy);
    Check(blocked,"First migration refuses an unreadable locked source without publishing a CTN");
    Put(root/legacyName,rich ? "{\\rtf1 Newer migration source}" : "Newer migration source");
    Check(!client_notes::WriteNotebook(root,pages,baseline) && !fs::exists(root/client_notes::kNotebookFileName),"First migration rejects a changed legacy source and keeps it authoritative");
    const auto latest=client_notes::ReadNotebook(root);Check(client_notes::WriteNotebook(root,pages,latest),"First migration succeeds with its exact refreshed legacy snapshot");
    const auto ctn=client_notes::ReadNotebook(root);auto newer=ctn.pages;newer.front().rtf=NotesPlainRtf(L"External CTN update");Check(client_notes::WriteNotebook(root,newer,ctn),"An independent writer updates the authoritative CTN");
    const auto external=client_notes::ReadNotebook(root).notebook_snapshot;
    Check(!client_notes::WriteNotebook(root,pages,ctn) && client_notes::ReadNotebook(root).notebook_snapshot.bytes==external.bytes,"Ignoring obsolete fallbacks preserves real authoritative CTN save-conflict protection");
    Put(root/client_notes::kNotebookFileName,"CTNBOOK1 malformed");
    Check(client_notes::ReadNotebook(root).status==client_notes::ReadStatus::Error,"A malformed authoritative CTN never silently falls back to legacy text");
  }
  NotesBackgroundAudit("round4 migration and authoritative conflict safeguards");
  NotesRound4DeletedRecovery();
  NotesRound4RecoveryRefusal();
}
