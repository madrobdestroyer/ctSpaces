struct UnownedExtractionAddition {
  fs::path path;
  unsigned mode=0;
  bool attempted=false;
};
static void AddUnownedExtractionObject(void* user,_7zOp operation,unsigned percent,const wchar_t* item) {
  auto& state=*static_cast<UnownedExtractionAddition*>(user);
  if(state.attempted || operation!=_7zOp::Extract || percent!=100 || item)return;
  state.attempted=true;
  if(state.mode==2)fs::create_directory(state.path);
  else {fs::create_directories(state.path.parent_path());if(!WriteBytes(state.path,{'F','O','R','E','I','G','N'}))throw std::runtime_error("Unowned extraction fixture write failed");}
}
static BY_HANDLE_FILE_INFORMATION OutputObjectInformation(const fs::path& path) {
  HANDLE file=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
      nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  BY_HANDLE_FILE_INFORMATION info{};
  if(file==INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file,&info)) {
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);throw std::runtime_error("Output object identity unavailable");
  }
  CloseHandle(file);return info;
}
static void TestArchiveOutputOwnership(const fs::path& root) {
  unsigned checks=0;const auto expect=[&](bool condition,const char* label){if(!condition)throw std::runtime_error(label);++checks;};
  const auto source=root/L"d/Sites",backup=root/L"backup.7z";
  const std::vector<unsigned char> original={'S','E','L','E','C','T','E','D'},foreign={'F','O','R','E','I','G','N'},old={'O','L','D'};
  fs::create_directories(source/L"Alpha/Default");fs::create_directory(source/L"Alpha/owned-empty");
  expect(WriteBytes(source/L"Alpha/Default/state.bin",original) && WriteBytes(source/L"Alpha/readonly.bin",original),"Owned-output fixture writes exact ordinary and read-only source data");
  expect(SetFileAttributesW((source/L"Alpha/readonly.bin").c_str(),FILE_ATTRIBUTE_READONLY)!=FALSE &&
      SetFileAttributesW((source/L"Alpha/owned-empty").c_str(),FILE_ATTRIBUTE_READONLY)!=FALSE,"Owned-output fixture preserves read-only file and directory attributes");
  expect(CtBackup::CreateValidated7zBackup(source,backup,CTSPACES_VERSION_TEXT,nullptr,nullptr).ok(),"Ownership fixture creates a real SDK backup of read-only and empty client data");
  const auto backupBytes=ReadBytes(backup);
  CtBackup::StagedRestore positive;
  expect(CtBackup::StageValidated7zRestore(backup,root/L"positive",nullptr,nullptr,positive).ok(),"Complete owned inventory accepts an ordinary valid backup");
  expect(ReadBytes(positive.payloadDir/L"Alpha/readonly.bin")==original &&
      (GetFileAttributesW((positive.payloadDir/L"Alpha/readonly.bin").c_str())&FILE_ATTRIBUTE_READONLY),"Accepted inventory preserves read-only payload bytes and attributes");
  expect(fs::is_empty(positive.payloadDir/L"Alpha/owned-empty"),"Accepted inventory includes genuine empty directories");
  for(unsigned mode=0;mode<3;++mode) {
    const auto stage=root/(L"s"+std::to_wstring(mode)),live=root/(L"l"+std::to_wstring(mode))/L"Sites";
    fs::create_directories(live/L"Keep");expect(WriteBytes(live/L"Keep/state.bin",old),"Unowned-output fixture writes distinct existing live data");
    const auto target=mode==0?stage/L"Sites/Alpha/foreign.bin":mode==1?stage/L"Sites/Alpha/unexpected/foreign.bin":stage/L"Sites/Alpha/foreign-empty";
    UnownedExtractionAddition mutation{target,mode};CtBackup::StagedRestore rejected;
    const auto result=CtBackup::StageValidated7zRestore(backup,stage,AddUnownedExtractionObject,&mutation,rejected);
    expect(mutation.attempted && !result.ok() && rejected.validation==nullptr && rejected.stagingRoot.empty(),"Unowned nested files or directories cannot become part of a commit-capable validation seal");
    expect(mode==2?fs::is_directory(target)&&fs::is_empty(target):ReadBytes(target)==foreign,"Rejected extraction retains every exact foreign file or empty directory");
    expect(ReadBytes(live/L"Keep/state.bin")==old && ReadBytes(source/L"Alpha/Default/state.bin")==original && ReadBytes(backup)==backupBytes,"Rejected output leaves old live, source and backup bytes unchanged");
    expect(!fs::exists(stage/L"ctSpacesBackup.manifest") && !fs::exists(stage/L"Sites/Alpha/Default/state.bin") &&
        !fs::exists(stage/L"Sites/Alpha/readonly.bin") && !fs::exists(stage/L"Sites/Alpha/owned-empty"),"Failure cleanup removes only original owned outputs, including read-only files and empty directories");
    expect(result.details.find(stage.wstring())!=std::wstring::npos,"Rejected unowned output reports its retained exact staging path");
  }
  {
    CtBackup::StagedRestore sealed;const auto stage=root/L"late",live=root/L"ll/Sites",recovery=root/L"late-recovery";
    expect(CtBackup::StageValidated7zRestore(backup,stage,nullptr,nullptr,sealed).ok(),"Late-addition fixture first obtains a complete valid owned seal");
    fs::create_directories(live/L"Keep");expect(WriteBytes(live/L"Keep/state.bin",old) && WriteBytes(stage/L"Sites/Alpha/after-seal.bin",foreign),"A separate writer adds data only after staging returns its seal");
    expect(!CtBackup::CommitStagedRestore(sealed,live,recovery).ok(),"After-seal additions are rejected by normal sealed-tree commit revalidation");
    expect(ReadBytes(live/L"Keep/state.bin")==old && !fs::exists(recovery) && ReadBytes(stage/L"Sites/Alpha/after-seal.bin")==foreign,"Late rejection preserves old live and independently added data without moving the old tree");
  }
  {
    const auto generic=root/L"generic";_7zArchiveInfo info{};_7zRestoreSession* session=nullptr;
    expect(SUCCEEDED(_7zOpenRestoreSession(backup.c_str(),&info,&session)),"Identity inventory fixture opens the real SDK archive");
    struct Close { _7zRestoreSession* value;~Close(){_7zCloseRestoreSession(value);} } close{session};
    expect(SUCCEEDED(_7zExtractRestoreSession(session,generic.c_str(),nullptr,nullptr,true)),"Generic SDK extraction retains its actual ownership records");
    std::vector<_7zRestoreObjectIdentity> inventory;
    const auto add=[&](const fs::path& path){const auto value=OutputObjectInformation(path);inventory.push_back({path==generic?L"":path.lexically_relative(generic).native(),value.dwVolumeSerialNumber,value.nFileIndexHigh,value.nFileIndexLow,(value.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0});};
    add(generic);for(const auto& entry:fs::recursive_directory_iterator(generic))add(entry.path());
    expect(SUCCEEDED(_7zVerifyRestoreOutputInventory(session,inventory)),"Inventory matches exact normalized paths, types and created identities");
    auto changed=inventory;++changed.back().indexLow;
    expect(FAILED(_7zVerifyRestoreOutputInventory(session,changed)),"Same path and type with a different object identity are rejected");
    changed=inventory;changed.back().directory=!changed.back().directory;
    expect(FAILED(_7zVerifyRestoreOutputInventory(session,changed)),"A changed object type cannot match an original inventory entry");
    changed=inventory;changed.back().relativePath=changed.front().relativePath;
    expect(FAILED(_7zVerifyRestoreOutputInventory(session,changed)),"Duplicate sealed aliases cannot disguise a missing owned object");
    changed=inventory;changed.pop_back();
    expect(FAILED(_7zVerifyRestoreOutputInventory(session,changed)),"An incomplete inventory is rejected even when every remaining entry matches");
  }
  {
    const auto manifest=root/L"invalid.manifest",invalid=root/L"invalid.7z",stage=root/L"readonly-failure";
    expect(WriteBackupManifest(manifest,99) && SUCCEEDED(_7zCompress7zWithExtraFile(invalid.c_str(),source.c_str(),true,manifest.c_str(),L"ctSpacesBackup.manifest",nullptr,nullptr)),"Read-only cleanup fixture has valid CRC and deliberately invalid manifest count");
    CtBackup::StagedRestore rejected;
    expect(!CtBackup::StageValidated7zRestore(invalid,stage,nullptr,nullptr,rejected).ok() && !fs::exists(stage),"Unrelated semantic rejection still fully cleans owned read-only extraction output");
  }
  {
    const auto legacy=root/L"legacy",live=root/L"le/Sites",recovery=root/L"legacy-recovery";
    fs::create_directories(legacy/L"Sites/Legacy");fs::create_directories(live.parent_path());
    expect(WriteBytes(legacy/L"Sites/Legacy/state.bin",original) && WriteBackupManifest(legacy/L"ctSpacesBackup.manifest",1),"Legacy-shaped normalized fixture includes backend-generated manifest metadata");
    CtBackup::StagedRestore normalized{legacy,legacy/L"Sites",1};
    expect(CtBackup::CaptureStagedRestoreIdentity(normalized).ok() && CtBackup::CommitStagedRestore(normalized,live,recovery).ok(),"Manual legacy normalization remains compatible without imposing an unrelated SDK inventory");
    expect(ReadBytes(live/L"Legacy/state.bin")==original && !fs::exists(legacy),"Legacy-shaped commit retains exact client bytes and cleans its generated manifest");
  }
  // Remove attributes only from these known synthetic copies for test teardown.
  for(const auto& tree:{source,positive.payloadDir,root/L"late/Sites",root/L"generic/Sites"}) {
    SetFileAttributesW((tree/L"Alpha/readonly.bin").c_str(),FILE_ATTRIBUTE_NORMAL);
    SetFileAttributesW((tree/L"Alpha/owned-empty").c_str(),FILE_ATTRIBUTE_NORMAL);
  }
  std::cout<<"SDK output inventory: "<<checks<<" checks passed (nested additions, identities, readonly, late seal, legacy compatibility).\n";
}
