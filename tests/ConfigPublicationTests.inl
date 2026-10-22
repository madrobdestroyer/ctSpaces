struct PublicationAttack {
  unsigned mode=0;
  bool attempted=false,changed=false,secondChanged=false;
  HANDLE rollbackLock=INVALID_HANDLE_VALUE;
  ~PublicationAttack(){if(rollbackLock!=INVALID_HANDLE_VALUE)CloseHandle(rollbackLock);}
};

void AttackConfigurationPublication(const fs::path& config,const fs::path& backup,
    config_persistence::IniPublicationBoundary boundary,void* user) {
  auto& attack=*static_cast<PublicationAttack*>(user);
  using Boundary=config_persistence::IniPublicationBoundary;
  if(boundary==Boundary::BeforePublish) {
    attack.attempted=true;
    if(attack.mode==0) {
      std::ofstream writer(config,std::ios::binary|std::ios::trunc);
      writer<<"FIRST EXTERNAL WRITER";attack.changed=static_cast<bool>(writer);
    } else if(attack.mode==7) {
      for(const auto& entry:fs::directory_iterator(config.parent_path())) {
        if(!entry.is_regular_file() || entry.path()==config)continue;
        const auto replacement=config.parent_path()/L"stage-writer.ini";
        WriteBytes(replacement,"FOREIGN STAGE WRITER");
        if(ReplaceFileW(entry.path().c_str(),replacement.c_str(),nullptr,
             REPLACEFILE_IGNORE_MERGE_ERRORS,nullptr,nullptr)) {attack.changed=true;break;}
        DeleteFileW(replacement.c_str());
      }
    } else if(attack.mode!=6 && attack.mode<8) {
      const auto replacement=config.parent_path()/L"external.ini";
      WriteBytes(replacement,"FIRST EXTERNAL WRITER");
      attack.changed=(attack.mode==1?MoveFileExW(replacement.c_str(),config.c_str(),MOVEFILE_REPLACE_EXISTING):
          ReplaceFileW(config.c_str(),replacement.c_str(),nullptr,REPLACEFILE_IGNORE_MERGE_ERRORS,nullptr,nullptr))!=FALSE;
      if(attack.mode==4)WriteBytes(backup,"FOREIGN BACKUP COLLISION");
    }
  } else if(boundary==Boundary::BeforeNativePublication && attack.mode>=8) {
    attack.attempted=true;
    const auto replacement=config.parent_path()/L"stage-native-writer.ini";
    WriteBytes(replacement,"FOREIGN STAGE WRITER");
    attack.changed=ReplaceFileW(backup.c_str(),replacement.c_str(),nullptr,
        REPLACEFILE_IGNORE_MERGE_ERRORS,nullptr,nullptr)!=FALSE;
  } else if(boundary==Boundary::BeforeRollback && attack.mode==3) {
    const auto replacement=config.parent_path()/L"second.ini";
    WriteBytes(replacement,"SECOND EXTERNAL WRITER");
    attack.secondChanged=ReplaceFileW(config.c_str(),replacement.c_str(),nullptr,
        REPLACEFILE_IGNORE_MERGE_ERRORS,nullptr,nullptr)!=FALSE;
  } else if(boundary==Boundary::BeforeRollback && attack.mode==5) {
    const auto displaced=backup.parent_path()/L"previous";
    attack.rollbackLock=CreateFileW(displaced.c_str(),GENERIC_READ,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  } else if(boundary==Boundary::AfterPublish && attack.mode==6) {
    const auto replacement=config.parent_path()/L"after.ini";
    WriteBytes(replacement,"FIRST EXTERNAL WRITER");
    attack.changed=ReplaceFileW(config.c_str(),replacement.c_str(),nullptr,
        REPLACEFILE_IGNORE_MERGE_ERRORS,nullptr,nullptr)!=FALSE;
  }
}

void TestProtectedConfigurationPublication() {
  unsigned checks=0;
  auto verify=[&](bool condition,const char* message){Expect(condition,message);++checks;};
  for(unsigned mode=0;mode<10;++mode) {
    TemporaryDirectory temporary;
    const auto config=temporary.path()/L"config.ini";
    const std::string original="[user]\r\ntheme=2\r\nkeep=ORIGINAL\r\n";
    if(mode!=9)WriteBytes(config,original);
    PublicationAttack attack;attack.mode=mode;
    config_persistence::SetIniPublicationHookForTest(AttackConfigurationPublication,&attack);
    struct ResetHook{~ResetHook(){config_persistence::SetIniPublicationHookForTest(nullptr,nullptr);}} reset;
    std::wstring details;
    const bool saved=config_persistence::ApplyIniMutationsAtomically(config,
        {{L"user",std::wstring(L"theme"),std::wstring(L"3")}},&details);
    verify(attack.attempted,"Publication adversary reaches actual commit boundary");
    bool first=false,second=false,collision=false,old=false,foreignStage=false;
    for(const auto& entry:fs::recursive_directory_iterator(temporary.path())) {
      if(!entry.is_regular_file())continue;
      const auto bytes=ReadBytes(entry.path());
      first|=bytes=="FIRST EXTERNAL WRITER";second|=bytes=="SECOND EXTERNAL WRITER";
      collision|=bytes=="FOREIGN BACKUP COLLISION";old|=bytes==original;
      foreignStage|=bytes=="FOREIGN STAGE WRITER";
    }
    if(mode<2) {
      verify(!attack.changed && saved,"Final original read lock blocks ordinary concurrent writes and replacement");
      verify(ReadIniValue(config,L"user",L"theme")==L"3","Protected atomic save commits intended settings");
      verify(details.empty(),"Successful atomic publication has no spurious error");
    } else if(mode>=8) {
      verify(attack.changed && !saved,"Native boundary staged-source substitution never reports a successful settings save");
      verify(foreignStage && (mode==9 || old),"Native staged-source substitution retains foreign bytes and any original config");
      verify(!details.empty(),"Native staged-source conflict reports retained data locations");
    } else if(mode==7) {
      verify(attack.changed && !saved,"Foreign replacement of the staged source is detected before native publication");
      verify(ReadBytes(config)==original && foreignStage,"Foreign stage object and original live config both remain byte-exact");
      verify(details.find(L"staging data was retained at")!=std::wstring::npos,"Foreign staged-file rejection reports its exact retained path");
    } else {
      verify(attack.changed && !saved,"Bypassing native replacement is reported as a save conflict");
      verify(first,"First external writer remains byte-exact in live config or recovery");
      verify(!details.empty(),"Conflict includes recovery guidance");
      if(mode==3) {
        verify(attack.secondChanged && second,"Rollback-time writer remains byte-exact in separate recovery");
        verify(ReadBytes(config)=="FIRST EXTERNAL WRITER","Conditional rollback restores the displaced newer config");
      } else if(mode==4) {
        verify(collision,"Preexisting private recovery-name collision is never overwritten or deleted");
        verify(ReadBytes(config)=="FIRST EXTERNAL WRITER","Rejected publication retains the already installed newer config");
      } else if(mode==5) {
        verify(attack.rollbackLock!=INVALID_HANDLE_VALUE,"Rollback failure fixture locks the displaced writer");
        verify(details.find(L"Recovery:")!=std::wstring::npos,"Failed rollback reports the exact retained recovery directory");
      } else if(mode==6) {
        verify(old && ReadBytes(config)=="FIRST EXTERNAL WRITER","Post-publication replacement retains external live config and original displaced config");
      } else verify(ReadBytes(config)=="FIRST EXTERNAL WRITER","Competing final replacement is restored without a deliberate two-rename gap");
    }
    verify(!fs::exists(temporary.path()/L"external.ini") || mode==1,"No consumed replacement source is misreported as successful mutation");
  }
  std::cout<<"Protected config publication: "<<checks<<" checks passed (10 scenarios).\n";
}

void TestPartialConfigurationPublication() {
  unsigned checks=0;
  auto verify=[&](bool condition,const char* message){Expect(condition,message);++checks;};
  for(const auto failure:{config_persistence::IniPublicationFailure::OldDisplaced,
                          config_persistence::IniPublicationFailure::Installed}) {
    TemporaryDirectory temporary;
    const auto config=temporary.path()/L"config.ini";
    const std::string original="[user]\r\ntheme=2\r\nkeep=ORIGINAL\r\n";
    WriteBytes(config,original);
    config_persistence::FailIniPublicationForTest(failure);
    std::wstring details;
    const bool saved=config_persistence::ApplyIniMutationsAtomically(config,
        {{L"user",std::wstring(L"theme"),std::wstring(L"3")}},&details);
    verify(!saved,"Native partial publication failure is reported to the caller");
    verify(ReadBytes(config)==original,"Native partial publication restores exact original live configuration bytes");
    verify(details.find(L"Recovery:")!=std::wstring::npos,"Partial publication reports its retained recovery directory");
    bool pending=false;
    for(const auto& entry:fs::recursive_directory_iterator(temporary.path())) {
      if(entry.is_regular_file() && entry.path()!=config &&
         HasUtf16LeBom(ReadBytes(entry.path())) && ReadIniValue(entry.path(),L"user",L"theme")==L"3")pending=true;
    }
    verify(pending,"Partial native failure retains the proposed replacement bytes for recovery");
    verify(config_persistence::ApplyIniMutationsAtomically(config,
        {{L"user",std::wstring(L"theme"),std::wstring(L"4")}},&details),"Fresh atomic save succeeds after a partial native failure");
    verify(ReadIniValue(config,L"user",L"theme")==L"4","Retry installs its intended setting");
  }
  std::cout<<"Partial native config publication: "<<checks<<" checks passed (2 scenarios).\n";
}
