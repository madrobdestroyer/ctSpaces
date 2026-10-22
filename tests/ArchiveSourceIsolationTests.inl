struct SnapshotBoundaryMutation {
  fs::path target, aside, outside;
  unsigned mode=0;
  bool attempted=false, changed=false;
};

static void AttackBackupSnapshot(void* user,_7zOp operation,unsigned,const wchar_t*) {
  auto& state=*static_cast<SnapshotBoundaryMutation*>(user);
  if(operation!=_7zOp::Compress || state.attempted)return;
  state.attempted=true;
  std::error_code error;
  switch(state.mode) {
  case 0: state.changed=WriteBytes(state.target/L"state.bin",{'C','H','A','N','G','E','D','!'});break;
  case 1:
    fs::rename(state.target,state.aside,error);
    if(!error)state.changed=CreateFixtureJunction(state.target,state.outside);
    break;
  case 2: state.changed=WriteBytes(state.target/L"added.bin",{'A','D','D','E','D'});break;
  case 3:
    fs::rename(state.target/L"state.bin",state.aside,error);
    state.changed=!error;
    break;
  case 4: state.changed=CreateFixtureJunction(state.target/L"empty",state.outside,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE);break;
  }
}

static void AttackExtractionSnapshot(void* user,_7zOp operation,unsigned percent,const wchar_t*) {
  auto& state=*static_cast<SnapshotBoundaryMutation*>(user);
  if(operation!=_7zOp::Extract || state.attempted || (state.mode<2 && percent!=100))return;
  state.attempted=true;
  std::error_code error;
  if(state.mode<2) {
    fs::rename(state.target,state.aside,error);state.changed=!error;
    if(state.changed) {fs::create_directory(state.target);WriteBytes(state.target/L"foreign.bin",{'F','O','R','E','I','G','N'});}
  } else if(state.mode==2)state.changed=WriteBytes(state.target/L"foreign.bin",{'F','O','R','E','I','G','N'});
  else if(state.mode==3) {
    fs::create_directories(state.target/L"Sites/Alpha/Default");
    state.changed=WriteBytes(state.target/L"Sites/Alpha/Default/state.bin",{'F','O','R','E','I','G','N'});
  } else state.changed=CreateFixtureJunction(state.target,state.outside,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE);
}

static void TestArchiveSourceIsolation(const fs::path& root) {
  unsigned checks=0;
  const auto expect=[&](bool condition,const char* description) {
    if(!condition)throw std::runtime_error(description);
    ++checks;
  };
  const std::vector<unsigned char> original={'O','R','I','G','I','N','A','L'};
  const std::vector<unsigned char> foreign={'F','O','R','E','I','G','N'};
  for(unsigned mode=0;mode<5;++mode) {
    const auto fixture=root/(L"source-boundary-"+std::to_wstring(mode));
    const auto source=fixture/L"d/Sites",backup=fixture/L"backup.7z";
    fs::create_directories(source/L"Alpha/Default");fs::create_directory(fixture/L"outside");
    if(mode==4)fs::create_directory(source/L"Alpha/Default/empty");
    expect(WriteBytes(source/L"Alpha/Default/state.bin",original),"Source boundary fixture write");
    expect(WriteBytes(fixture/L"outside/state.bin",foreign),"Outside boundary fixture write");
    expect(WriteBytes(backup,foreign),"Previous backup sentinel write");
    SnapshotBoundaryMutation attack{source/L"Alpha/Default",fixture/L"aside",fixture/L"outside",mode};
    const auto result=CtBackup::CreateValidated7zBackup(source,backup,CTSPACES_VERSION_TEXT,AttackBackupSnapshot,&attack);
    expect(attack.attempted,"Source attack reaches the compression boundary");
    if(mode==2) {
      expect(attack.changed && !result.ok(),"Source addition rejects an inconsistent snapshot");
      expect(ReadBytes(backup)==foreign,"Rejected snapshot preserves the old destination exactly");
      expect(ReadBytes(attack.target/L"added.bin")==std::vector<unsigned char>({'A','D','D','E','D'}),"Concurrent added source data is retained");
    } else {
      expect(!attack.changed && result.ok(),"Write, junction and leaf substitution are blocked by bound source objects");
      CtBackup::StagedRestore staged;
      expect(CtBackup::StageValidated7zRestore(backup,fixture/L"stage",nullptr,nullptr,staged).ok(),"Protected backup restores successfully");
      expect(ReadBytes(staged.payloadDir/L"Alpha/Default/state.bin")==original,"Restored backup contains exact original selected-client bytes");
    }
    expect(ReadBytes(source/L"Alpha/Default/state.bin")==original,"Source bytes are unchanged after each boundary attack");
    expect(ReadBytes(fixture/L"outside/state.bin")==foreign,"Outside-client bytes are unchanged after each boundary attack");
  }
  const auto fixture=root/L"extraction-boundary",source=fixture/L"d/Sites",backup=fixture/L"backup.7z";
  fs::create_directories(source/L"Alpha/Default");
  expect(WriteBytes(source/L"Alpha/Default/state.bin",original),"Extraction boundary fixture write");
  expect(CtBackup::CreateValidated7zBackup(source,backup,CTSPACES_VERSION_TEXT,nullptr,nullptr).ok(),"Extraction boundary backup creation");
  fs::create_directory(fixture/L"outside");
  expect(WriteBytes(fixture/L"outside/foreign.bin",foreign),"Extraction reparse outside sentinel");
  for(unsigned mode=0;mode<5;++mode) {
    const auto stage=fixture/(L"stage"+std::to_wstring(mode));
    SnapshotBoundaryMutation attack{mode==1?stage/L"Sites/Alpha/Default":stage,fixture/(L"aside"+std::to_wstring(mode)),fixture/L"outside",mode};
    CtBackup::StagedRestore staged;
    const auto result=CtBackup::StageValidated7zRestore(backup,stage,AttackExtractionSnapshot,&attack,staged);
    expect(attack.attempted,"Extraction attack reaches its intended callback boundary");
    if(mode<2 || mode==4) {
      expect(!attack.changed && result.ok(),"Original extraction root and payload directories cannot be substituted");
      expect(ReadBytes(staged.payloadDir/L"Alpha/Default/state.bin")==original,"Protected extraction keeps the original payload bytes");
      if(mode==4)expect(ReadBytes(fixture/L"outside/foreign.bin")==foreign && !fs::exists(fixture/L"outside/Sites"),"In-place root reparse attack cannot redirect extraction into outside data");
    } else {
      expect(attack.changed && !result.ok(),"Foreign extraction additions cause safe rejection");
      expect(ReadBytes(mode==2?stage/L"foreign.bin":stage/L"Sites/Alpha/Default/state.bin")==foreign,"Failed extraction cleanup never deletes or overwrites foreign additions");
      expect(fs::is_directory(stage),"Nonempty recovery staging is retained for the foreign owner");
      if(mode==2)expect(!fs::exists(stage/L"Sites") && !fs::exists(stage/L"ctSpacesBackup.manifest"),"Owned extraction objects are cleaned without recursive deletion");
    }
  }
  std::cout<<"Archive source/extraction boundary regressions: "<<checks<<" checks passed.\n";
}

static bool AttackRestoreParent(void* user,std::wstring&) {
  auto& state=*static_cast<SnapshotBoundaryMutation*>(user);state.attempted=true;
  if(state.mode==0) {
    std::error_code error;fs::rename(state.target,state.aside,error);state.changed=!error;
    if(state.changed)fs::create_directory(state.target);
  } else state.changed=CreateFixtureJunction(state.target,state.outside,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE);
  return true;
}

static void TestRestoreDestinationParentBindings(const fs::path& root) {
  unsigned checks=0;
  const auto expect=[&](bool condition,const char* description){if(!condition)throw std::runtime_error(description);++checks;};
  for(unsigned mode=0;mode<2;++mode) {
    const auto fixture=root/(L"p"+std::to_wstring(mode));
    const auto source=fixture/L"d/Sites",backup=fixture/L"b.7z",live=fixture/L"l/Sites",parent=fixture/L"r",stage=fixture/L"s";
    fs::create_directories(source/L"Alpha");fs::create_directories(live/L"Keep");fs::create_directory(parent);fs::create_directory(fixture/L"o");
    expect(WriteBytes(source/L"Alpha/state.bin",{'S','O','U','R','C','E'}),"Recovery-parent source write");
    expect(WriteBytes(live/L"Keep/state.bin",{'L','I','V','E'}),"Recovery-parent live write");
    expect(WriteBytes(fixture/L"o/sentinel",{'O','U','T','S','I','D','E'}),"Recovery-parent outside sentinel");
    expect(CtBackup::CreateValidated7zBackup(source,backup,CTSPACES_VERSION_TEXT,nullptr,nullptr).ok(),"Recovery-parent backup creation");
    CtBackup::StagedRestore staged;
    expect(CtBackup::StageValidated7zRestore(backup,stage,nullptr,nullptr,staged).ok(),"Recovery-parent restore stages safely");
    SnapshotBoundaryMutation attack{parent,fixture/L"a",fixture/L"o",mode};
    const auto result=CtBackup::CommitStagedRestore(staged,live,parent/L"old",AttackRestoreParent,&attack);
    expect(attack.attempted && !attack.changed,"Read/list parent leases and owned sentinels prevent rename and in-place junction substitution");
    expect(result.ok(),"Unchanged protected restore parents allow normal native publication");
    expect(ReadBytes(parent/L"old/Keep/state.bin")==std::vector<unsigned char>({'L','I','V','E'}),"Original live bytes move into the original recovery parent");
    expect(ReadBytes(live/L"Alpha/state.bin")==std::vector<unsigned char>({'S','O','U','R','C','E'}),"Selected backup installs exact payload bytes");
    expect(ReadBytes(fixture/L"o/sentinel")==std::vector<unsigned char>({'O','U','T','S','I','D','E'}) && !fs::exists(fixture/L"o/old"),"Outside recovery-parent data is untouched");
    expect(std::distance(fs::directory_iterator(parent),fs::directory_iterator{})==1,"Owned parent sentinels are removed after successful restore");
  }
  std::cout<<"Restore destination parent bindings: "<<checks<<" checks passed.\n";
}

static void TestOwnedExtractionCleanupRetries(const fs::path& root) {
  unsigned checks=0;
  const auto expect=[&](bool condition,const char* description){if(!condition)throw std::runtime_error(description);++checks;};
  const auto fixture=root/L"cl",source=fixture/L"d/Sites",archive=fixture/L"a.7z",manifest=fixture/L"m";
  fs::create_directories(source/L"Alpha/empty");
  const std::vector<unsigned char> bytes={'E','X','A','C','T',' ','S','O','U','R','C','E'};
  expect(WriteBytes(source/L"Alpha/data.bin",bytes),"Cleanup retry source fixture write");
  expect(WriteBackupManifest(manifest,99),"Cleanup retry intentionally mismatched manifest write");
  expect(SUCCEEDED(_7zCompress7zWithExtraFile(archive.c_str(),source.c_str(),true,manifest.c_str(),
      L"ctSpacesBackup.manifest",nullptr,nullptr)),"Cleanup retry fixture has valid CRC and invalid declared count");
  for(const unsigned denied:{4u,8u}) {
    const auto stage=fixture/(L"s"+std::to_wstring(denied));CtBackup::StagedRestore staged;
    _7zDenyCleanupParentAttemptsForTest(denied);
    const auto result=CtBackup::StageValidated7zRestore(archive,stage,nullptr,nullptr,staged);
    const unsigned observed=_7zCleanupParentAttemptsForTest();_7zDenyCleanupParentAttemptsForTest(0);
    expect(!result.ok() && staged.stagingRoot.empty(),"Invalid count is rejected while cleanup retry logic runs");
    expect(observed==(denied==4?5u:8u),"Owned directory disposition retries are bounded and stop at success/exhaustion");
    expect(ReadBytes(source/L"Alpha/data.bin")==bytes,"Cleanup retry/exhaustion never changes the source backup bytes");
    if(denied==4)expect(!fs::exists(stage),"Transient directory-not-empty errors leave no owned staging root");
    else {
      expect(fs::is_directory(stage),"Exhausted safe disposition retains owned staging instead of unsafe recursive deletion");
      fs::remove_all(stage);
    }
  }
  std::cout<<"Owned extraction cleanup retries: "<<checks<<" checks passed.\n";
}
