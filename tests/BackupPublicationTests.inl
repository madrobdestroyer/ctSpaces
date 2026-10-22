struct BackupOutputMutation {
  fs::path root,parent,aside,selected;
  unsigned mode=0;
  bool attempted=false,changed=false;
  std::vector<unsigned char> validReplacement;
};
static void MutateBackupOutput(const fs::path& manifest,const fs::path& archive,
    CtBackup::BackupPublicationBoundary boundary,void* user) {
  auto& mutation=*static_cast<BackupOutputMutation*>(user);
  const auto wanted=mutation.mode==4 || mutation.mode==10?CtBackup::BackupPublicationBoundary::ManifestCreated:
      (mutation.mode<2 || mutation.mode==6 || mutation.mode==9?CtBackup::BackupPublicationBoundary::ArchiveCreated:CtBackup::BackupPublicationBoundary::Verified);
  if(mutation.attempted || boundary!=wanted)return;
  mutation.attempted=true;mutation.selected=mutation.mode==4 || mutation.mode==10?manifest:archive;
  std::error_code error;
  if(mutation.mode==10){auto bytes=ReadBytes(manifest);const std::string text(bytes.begin(),bytes.end());const auto count=text.find("profileCount=1");if(count==std::string::npos)throw std::runtime_error("Manifest mutation fixture expected one profile");bytes[count+13]='2';mutation.changed=WriteBytes(manifest,bytes);return;}
  if(mutation.mode==9){mutation.changed=WriteBytes(archive,mutation.validReplacement);return;}
  if(mutation.mode==6) {
    mutation.changed=CreateHardLinkW((mutation.root/L"side/external-alias.7z").c_str(),archive.c_str(),nullptr)!=FALSE;return;
  }
  if(mutation.mode==7) {mutation.changed=WriteBytes(archive,{'F','O','R','E','I','G','N'});return;}
  if(mutation.mode==8) {mutation.changed=WriteBytes(mutation.parent/L"backup.7z",{'N','E','W','E','R'});return;}
  if(mutation.mode==5) {
    fs::rename(mutation.parent,mutation.aside,error);mutation.changed=!error;
    if(mutation.changed) {fs::create_directory(mutation.parent);WriteBytes(mutation.parent/L"foreign.txt",{'F','O','R','E','I','G','N'});}return;
  }
  fs::rename(mutation.selected,mutation.aside,error);mutation.changed=!error;
  if(mutation.changed) {
    if(mutation.mode==1 || mutation.mode==3 || mutation.mode==4) {
      fs::create_directory(mutation.selected);WriteBytes(mutation.selected/L"foreign.txt",{'F','O','R','E','I','G','N'});
    } else WriteBytes(mutation.selected,{'U','N','V','E','R','I','F','I','E','D'});
  }
  if(mutation.mode==3)CtBackup::FailBackupPublicationForTest();
}

static void TestBackupPublicationOwnership(const fs::path& root) {
  unsigned checks=0;
  const auto expect=[&](bool condition,const char* label){if(!condition)throw std::runtime_error(label);++checks;};
  const std::vector<unsigned char> original={'S','E','L','E','C','T','E','D'},old={'O','L','D'},foreign={'F','O','R','E','I','G','N'};
  for(unsigned mode=0;mode<11;++mode) {
    const auto fixture=root/(L"publication-"+std::to_wstring(mode)),source=fixture/L"d/Sites",output=fixture/L"o";
    fs::create_directories(source/L"Alpha");fs::create_directory(output);fs::create_directory(fixture/L"side");
    expect(WriteBytes(source/L"Alpha/state.bin",original),"Publication fixture writes selected source bytes");
    const auto destination=output/L"backup.7z";expect(WriteBytes(destination,old),"Publication fixture writes old selected destination");
    BackupOutputMutation mutation{fixture,output,mode==5?fixture/L"old-output-parent":fixture/L"side"/(mode==4?L"original-manifest":L"original-verified.7z"),{},mode};
    if(mode==9) {
      const auto other=fixture/L"other/d/Sites";fs::create_directories(other/L"Alpha");
      expect(WriteBytes(other/L"Alpha/state.bin",foreign),"Same-identity rewrite fixture writes different client bytes");
      expect(CtBackup::CreateValidated7zBackup(other,fixture/L"other/valid.7z",CTSPACES_VERSION_TEXT,nullptr,nullptr).ok(),"Same-identity rewrite fixture creates a different valid archive");
      mutation.validReplacement=ReadBytes(fixture/L"other/valid.7z");
      expect(!mutation.validReplacement.empty(),"Same-identity rewrite uses actual complete SDK archive bytes");
    }
    CtBackup::SetBackupPublicationHookForTest(MutateBackupOutput,&mutation);
    struct Reset {~Reset(){CtBackup::SetBackupPublicationHookForTest(nullptr,nullptr);}} reset;
    const auto result=CtBackup::CreateValidated7zBackup(source,destination,CTSPACES_VERSION_TEXT,nullptr,nullptr);
    std::wcout<<L"Backup output scenario "<<mode<<L": mutation="<<mutation.changed<<L", result="<<result.code<<L", "<<result.details<<L"\n";
    expect(mutation.attempted,"Output mutation reaches its real creation/publication boundary");
    expect(ReadBytes(source/L"Alpha/state.bin")==original,"Each output-boundary scenario preserves exact client source bytes");
    if((mode<2 || mode==6) && mutation.changed) {
      expect(mutation.changed && !result.ok(),"An unbound stage substitution or extra hard link is rejected before publication");
      expect(ReadBytes(destination)==old,"Rejected staged output keeps exact old destination bytes");
      if(mode==0)expect(ReadBytes(mutation.selected)==std::vector<unsigned char>({'U','N','V','E','R','I','F','I','E','D'}),"Rejected publication retains foreign staged-file bytes");
      if(mode==1)expect(ReadBytes(mutation.selected/L"foreign.txt")==foreign,"Failure cleanup never recurses into a foreign staged directory");
      if(mode==6)expect(ReadBytes(mutation.selected)==ReadBytes(fixture/L"side/external-alias.7z"),"Unsafe linked stage and its outside alias remain byte-exact");
      else expect(SUCCEEDED(_7zTest7z(mutation.aside.c_str(),nullptr,nullptr)),"The displaced original output remains a valid recoverable archive");
      expect(result.details.find(mutation.selected.wstring())!=std::wstring::npos,"Rejected changed output reports its retained exact path");
    } else if(mode==10) {
      expect(mutation.changed && !result.ok() && ReadBytes(destination)==old,"Same-object manifest rewrite rejects before compression and preserves old backup bytes");
      const auto retained=ReadBytes(mutation.selected);const std::string text(retained.begin(),retained.end());
      expect(text.find("profileCount=2")!=std::string::npos,"Changed same-object manifest bytes are retained for their independent writer");
      expect(result.details.find(mutation.selected.wstring())!=std::wstring::npos,"Changed manifest content reports its retained exact path");
    } else if(mode==4 && mutation.changed) {
      expect(!result.ok() && ReadBytes(destination)==old,"Changed manifest prevents compression and preserves old destination");
      expect(ReadBytes(mutation.selected/L"foreign.txt")==foreign,"Manifest cleanup never deletes a foreign replacement directory");
      expect(result.details.find(mutation.selected.wstring())!=std::wstring::npos,"Changed manifest reports its retained path");
    } else if(mode==3) {
      expect(!mutation.changed && !result.ok(),"Final stage namespace is protected even when native publication fails");
      expect(ReadBytes(destination)==old,"Native promotion failure preserves exact old backup");
      expect(std::distance(fs::directory_iterator(output),fs::directory_iterator{})==1,"Native promotion failure cleans only its original stage and manifest");
    } else {
      expect((mode==8?mutation.changed:!mutation.changed) && result.ok(),mode==8?
          "Selected-destination replacement policy is preserved without claiming destination compare-and-swap":
          "Protected stages publish normally while archive writes and namespace changes are blocked");
      if(mode==9)expect(!mutation.changed,"The creation lease blocks a valid different archive from being written into the same object before validation");
      CtBackup::StagedRestore staged;
      expect(CtBackup::StageValidated7zRestore(destination,fixture/L"stage",nullptr,nullptr,staged).ok(),"Published original object passes real SDK restore staging");
      expect(ReadBytes(staged.payloadDir/L"Alpha/state.bin")==original,"Published backup restores exact selected client bytes");
      expect(std::distance(fs::directory_iterator(output),fs::directory_iterator{})==1,"Successful publication leaves only the selected output file");
    }
  }
  std::cout<<"Bound backup publication: "<<checks<<" checks passed (11 scenarios).\n";
}
