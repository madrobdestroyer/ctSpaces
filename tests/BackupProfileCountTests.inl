struct BackupProfileCountMutation {
  fs::path source,removed;
  unsigned mode=0;
  bool attempted=false,changed=false;
};
static void MutateBackupProfileCount(const fs::path&,const fs::path&,
    CtBackup::BackupPublicationBoundary boundary,void* user) {
  auto& mutation=*static_cast<BackupProfileCountMutation*>(user);
  if(mutation.attempted || boundary!=CtBackup::BackupPublicationBoundary::ManifestCreated)return;
  mutation.attempted=true;
  if(mutation.mode==2) {mutation.changed=WriteBytes(mutation.source/L"outside-client.txt",{'F','O','R','E','I','G','N'});return;}
  if(mutation.mode==1 || mutation.mode==3) {
    std::error_code error;fs::rename(mutation.source/L"Alpha",mutation.removed/L"Alpha",error);mutation.changed=!error;
    if(mutation.mode==1 || !mutation.changed)return;
  }
  {
    const auto client=mutation.mode==3?L"Default":L"Beta";
    fs::create_directories(mutation.source/client/L"Default");
    client_notes::NotePage page;page.name=L"Beta handover";page.id=1;page.rich=true;page.rtf="{\\rtf1\\ansi Beta private note}";
    std::string bytes;if(!client_notes::SerializeNotebook({page},bytes))throw std::runtime_error("Added client notebook serialization failed");
    mutation.changed=WriteBytes(mutation.source/client/client_notes::kNotebookFileName,{bytes.begin(),bytes.end()}) &&
        WriteBytes(mutation.source/client/L"Default/state.bin",{'B','E','T','A'});
  }
}
static void TestBackupProfileCountReconciliation(const fs::path& root) {
  unsigned checks=0;const auto expect=[&](bool condition,const char* label){if(!condition)throw std::runtime_error(label);++checks;};
  for(unsigned mode=0;mode<4;++mode) {
    const auto fixture=root/std::to_wstring(mode),source=fixture/L"d/Sites",backup=fixture/L"o/backup.7z";
    fs::create_directories(fixture/L"o");fs::create_directory(fixture/L"removed");
    client_notes::NotePage page;page.name=L"Alpha handover";page.id=1;page.rich=true;page.rtf="{\\rtf1\\ansi Alpha private note}";
    page.ticket=L"T20261014.0031";page.status=2;page.tags={L"handover"};std::string bytes;
    expect(client_notes::SerializeNotebook({page},bytes),"Count reconciliation fixture serializes actual client notebook metadata");
    fs::create_directories(source/L"Alpha/Default");
    expect(WriteBytes(source/L"Alpha"/client_notes::kNotebookFileName,{bytes.begin(),bytes.end()}) &&
        WriteBytes(source/L"Alpha/Default/state.bin",{'A','L','P','H','A'}),"Count reconciliation fixture writes selected Alpha data");
    if(mode==1) {BackupProfileCountMutation add{source,{},0};MutateBackupProfileCount({}, {},CtBackup::BackupPublicationBoundary::ManifestCreated,&add);expect(add.changed,"Removal fixture begins with a real second client notebook");}
    expect(CtBackup::CreateValidated7zBackup(source,backup,CTSPACES_VERSION_TEXT,nullptr,nullptr).ok(),"Stable original client set creates a valid baseline backup");
    const auto priorBackup=ReadBytes(backup),alphaNotebook=ReadBytes(source/L"Alpha"/client_notes::kNotebookFileName);
    BackupProfileCountMutation mutation{source,fixture/L"removed",mode};
    CtBackup::SetBackupPublicationHookForTest(MutateBackupProfileCount,&mutation);
    struct Reset {~Reset(){CtBackup::SetBackupPublicationHookForTest(nullptr,nullptr);}} reset;
    const auto result=CtBackup::CreateValidated7zBackup(source,backup,CTSPACES_VERSION_TEXT,nullptr,nullptr);
    CtBackup::SetBackupPublicationHookForTest(nullptr,nullptr);
    expect(mutation.attempted && mutation.changed,"Client-count or shape mutation occurs before the SDK binds its source snapshot");
    expect(!result.ok(),"Changed archived client count or invalid source shape cannot publish an unrestorable backup");
    expect(ReadBytes(backup)==priorBackup,"Semantic count rejection preserves exact prior backup bytes");
    expect(ReadBytes((mode==1 || mode==3?fixture/L"removed/Alpha":source/L"Alpha")/client_notes::kNotebookFileName)==alphaNotebook,"Count rejection preserves selected client's exact notebook bytes");
    expect(std::distance(fs::directory_iterator(fixture/L"o"),fs::directory_iterator{})==1,"Count rejection cleans only its owned temporary archive and manifest");
    CtBackup::StagedRestore old;
    expect(CtBackup::StageValidated7zRestore(backup,fixture/L"old-stage",nullptr,nullptr,old).ok(),"Previous backup remains completely restorable after count rejection");
    expect(old.profileCount==(mode==1?2:1),"Retained archive keeps the original client count");
    expect(ReadBytes(old.payloadDir/L"Alpha"/client_notes::kNotebookFileName)==alphaNotebook,"Retained archive restores exact Alpha notebook and metadata bytes");
    if(mode==2) {
      expect(ReadBytes(source/L"outside-client.txt")==std::vector<unsigned char>({'F','O','R','E','I','G','N'}),"Rejected root-file addition is retained with exact independent bytes");
      expect(fs::remove(source/L"outside-client.txt"),"The disposable fixture explicitly corrects its own invalid root file before retry");
    } else if(mode==3) {
      const auto changed=ReadBytes(source/L"Default"/client_notes::kNotebookFileName);
      fs::rename(source/L"Default",fixture/L"removed/Default");fs::rename(fixture/L"removed/Alpha",source/L"Alpha");
      expect(!changed.empty() && ReadBytes(fixture/L"removed/Default"/client_notes::kNotebookFileName)==changed,"Invalid renamed client data remains byte-exact while the disposable fixture corrects its shape");
    }
    expect(CtBackup::CreateValidated7zBackup(source,backup,CTSPACES_VERSION_TEXT,nullptr,nullptr).ok(),"Retry binds the new stable client set and publishes a coherent backup");
    CtBackup::StagedRestore retry;
    expect(CtBackup::StageValidated7zRestore(backup,fixture/L"retry-stage",nullptr,nullptr,retry).ok(),"Retried backup passes actual SDK extraction and manifest reconciliation");
    expect(retry.profileCount==(mode==0?2:1),"Retried archive's declared count matches its actual selected clients");
    const auto live=fixture/L"l/Sites",recovery=fixture/L"pre-restore";fs::create_directories(live/L"Keep");
    expect(WriteBytes(live/L"Keep/state.bin",{'O','L','D',' ','L','I','V','E'}),"Retry commit begins with different existing live data");
    const auto committed=CtBackup::CommitStagedRestore(retry,live,recovery);
    if(!committed.ok())std::wcerr<<L"Profile-count retry commit failed: "<<committed.code<<L" "<<committed.details<<L"\n";
    expect(committed.ok(),"Coherent retry installs through the actual sealed restore transaction");
    expect(ReadBytes(recovery/L"Keep/state.bin")==std::vector<unsigned char>({'O','L','D',' ','L','I','V','E'}),"Restore retains exact previous live data in recovery");
    expect(!fs::exists(retry.stagingRoot),"Successful coherent retry leaves no owned extraction staging");
    if(mode<2) {const auto beta=client_notes::ReadNotebook(live/L"Beta");expect(beta.status==client_notes::ReadStatus::Ok && beta.pages.size()==1 && beta.pages.front().name==L"Beta handover", "Installed retry reads the correct Beta notebook through production persistence");}
    if(mode!=1) {const auto alpha=client_notes::ReadNotebook(live/L"Alpha");expect(alpha.status==client_notes::ReadStatus::Ok && alpha.pages.front().ticket==page.ticket && alpha.pages.front().status==2 && alpha.pages.front().tags==page.tags,"Restored Alpha retains its own ticket, status and tags");}
    else expect(!fs::exists(live/L"Alpha") && ReadBytes(fixture/L"removed/Alpha"/client_notes::kNotebookFileName)==alphaNotebook,"A removed client stays outside the retry while its complete notebook remains recoverable");
    if(mode>=2)expect(!fs::exists(live/L"outside-client.txt") && !fs::exists(live/L"Default"),"Corrected retry restores only valid client roots");
  }
  {
    const auto fixture=root/L"u",source=fixture/L"d/Sites",backup=fixture/L"backup.7z",live=fixture/L"l/Sites",recovery=fixture/L"recovery";
    fs::create_directories(source/L"M\x00FCnchen");fs::create_directories(source/L"Empty client");fs::create_directories(live.parent_path());
    client_notes::NotePage page;page.name=L"M\x00FCnchen handover";page.id=1;page.rich=true;page.rtf="{\\rtf1\\ansi Unicode client private note}";std::string bytes;
    expect(client_notes::SerializeNotebook({page},bytes),"Valid Unicode client notebook serializes");
    expect(WriteBytes(source/L"M\x00FCnchen"/client_notes::kNotebookFileName,{bytes.begin(),bytes.end()}),"Valid Unicode client notebook writes actual metadata");
    expect(CtBackup::CreateValidated7zBackup(source,backup,CTSPACES_VERSION_TEXT,nullptr,nullptr).ok(),"Valid Unicode and empty client directories remain accepted by backup policy");
    CtBackup::StagedRestore stage;
    expect(CtBackup::StageValidated7zRestore(backup,fixture/L"stage",nullptr,nullptr,stage).ok() && stage.profileCount==2,"Unicode and empty clients reconcile through actual SDK staging");
    const auto committed=CtBackup::CommitStagedRestore(stage,live,recovery);
    if(!committed.ok())std::wcerr<<L"Unicode/empty client commit failed: "<<committed.code<<L" "<<committed.details<<L"\n";
    expect(committed.ok(),"Unicode and empty clients install into an initially absent live Sites");
    const auto notebook=client_notes::ReadNotebook(live/L"M\x00FCnchen");
    expect(notebook.status==client_notes::ReadStatus::Ok && notebook.pages.front().name==page.name,"Unicode client restores its exact notebook identity");
    expect(fs::is_directory(live/L"Empty client") && fs::is_empty(live/L"Empty client"),"Empty compatible client stays empty after restore");
    expect(ReadBytes(source/L"M\x00FCnchen"/client_notes::kNotebookFileName)==std::vector<unsigned char>(bytes.begin(),bytes.end()),"Accepted Unicode backup leaves exact source notebook bytes intact");
    expect(!fs::exists(recovery) && !fs::exists(stage.stagingRoot),"Initial restore leaves no unnecessary recovery or owned staging path");
  }
  std::cout<<"Backup profile-count and client-shape reconciliation: "<<checks<<" checks passed (4 mutations, Unicode and empty-client acceptance).\n";
}
