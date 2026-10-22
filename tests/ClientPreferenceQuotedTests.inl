// End-to-end identity round trips through production writers and native WinINI.
// Included by ClientPreferenceDeltaTests.cpp; all profiles and settings remain
// in its retained fixture, with the existing no-window/no-launch guards.
namespace quoted_client_preferences {
static const std::wstring quoted=L"'Quoted client'",alias=L"Quoted client";
static void AddClient(const std::wstring &name,const std::string &note){
  const auto root=g_sDataDir/L"Sites"/name;fs::create_directories(root);
  {std::ofstream marker(root/kClientSchemaMarkerName,std::ios::binary);marker<<kClientSchemaMarkerText;marker.close();Check(static_cast<bool>(marker),"Quoted identity fixture writes a private client marker");}
  {std::ofstream saved(root/client_notes::kFileName,std::ios::binary);saved<<note;saved.close();Check(static_cast<bool>(saved),"Quoted identity fixture writes private note bytes");}
}
static void Setup(bool canonical,bool createQuoted=true){
  Prepare({L"Beta"},{L"Gamma"});g_sessions.clear();g_activeProfiles.clear();
  g_sShortcutDesktopOverride=g_sDataDir/L"empty-quoted-shortcuts";fs::create_directory(g_sShortcutDesktopOverride);
  AddClient(alias,"UNQUOTED OTHER CLIENT MUST SURVIVE");
  if(createQuoted)AddClient(quoted,"QUOTED SELECTED CLIENT MUST SURVIVE");
  auto text=EncodeCollections({L"Beta"},{L"Gamma"});
  if(!canonical){
    std::wstring decoded(reinterpret_cast<const wchar_t *>(text.data()+2),(text.size()-2)/2);
    const auto count=decoded.find(L"count=1");decoded.replace(count,7,L"count = 0001");
    const auto row=decoded.find(L"client0=Beta");decoded.replace(row,12,L"client0 = Beta");
    text=EncodeText(decoded);
  }
  WriteCollections(text);
}
static void OtherIntact(){Check(Bytes(g_sDataDir/L"Sites"/alias/client_notes::kFileName)=="UNQUOTED OTHER CLIENT MUST SURVIVE","Quoted preference actions preserve the separate unquoted client's exact note bytes");}
static void PinArchiveCases(){
  for(bool canonical:{true,false})for(unsigned action=0;action<3;++action){
    Setup(canonical);const auto note=Bytes(g_sDataDir/L"Sites"/quoted/client_notes::kFileName);
    bool saved=false;
    if(action==0)saved=SaveClientPreferenceDelta(quoted,true,std::nullopt);
    else if(action==1){const auto expected=g_pinnedClients;g_pinnedClients.push_back(quoted);saved=SavePinnedClients(&expected);}
    else saved=SaveClientPreferenceDelta(quoted,std::nullopt,true);
    Check(saved,"Canonical and noncanonical production preference writers accept the valid quoted client identity");
    if(action<2){
      Check(DiskList(L"pinned")==std::vector<std::wstring>{L"Beta",quoted},"Native pinned reload retains the literal apostrophes and original ordered client identity");
      g_pinnedClients.clear();LoadPinnedClients();
      Check(g_pinnedClients==std::vector<std::wstring>{L"Beta",quoted}&&!IsClientPinned(alias),"Fresh production pinned load never targets the separate unquoted client");
      Check(SaveClientPreferenceDelta(quoted,false,std::nullopt)&&DiskList(L"pinned")==std::vector<std::wstring>{L"Beta"},"Removing the exact quoted pin preserves the unrelated ordered client");
    }else{
      Check(DiskList(L"archived")==std::vector<std::wstring>{L"Gamma",quoted},"Native archive reload retains the literal quoted client identity");
      g_archivedClients.clear();LoadArchivedClients();
      Check(IsClientArchived(quoted)&&!IsClientArchived(alias),"Fresh production archive load never archives the separate unquoted client");
      Check(SaveClientPreferenceDelta(quoted,std::nullopt,false)&&DiskList(L"archived")==std::vector<std::wstring>{L"Gamma"},"Restoring the exact quoted membership preserves the unrelated archive entry");
    }
    Check(Bytes(g_sDataDir/L"Sites"/quoted/client_notes::kFileName)==note,"Quoted membership round trips preserve selected client's note bytes");OtherIntact();CheckUnknown();
    std::cout<<"Quoted identity membership: canonical="<<canonical<<"; operation="<<action<<"; exact native identity roundtrip passed\n";
  }
}
static void RenameCases(){
  // Valid rename cases need the same short retained root as the established
  // rename suite: production reserves 150 characters for its starter files.
  struct Scope{fs::path previous=base;~Scope(){base=previous;}} scope;
  base=retained_fixture::CreateRoot(fs::current_path()/L"build",L"qcp");
  for(bool canonical:{true,false}){
    Setup(canonical,false);AddClient(alpha,"RENAMED CLIENT ORIGINAL NOTE");
    Check(CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(g_sDataDir/L"Sites",quoted),"Positive quoted rename fixture satisfies the actual managed path budget");
    g_pinnedClients={alpha,L"Beta"};g_archivedClients={L"Gamma"};Seed({alpha+L"|edge"});
    auto bytes=rename_preference_delta::Settings(g_pinnedClients,g_archivedClients,Preferences());
    if(!canonical){std::wstring text(reinterpret_cast<const wchar_t *>(bytes.data()+2),(bytes.size()-2)/2);text.replace(text.find(L"count=2"),7,L"count = 0002");bytes=EncodeText(text);}
    WriteCollections(bytes);std::wstring error;
    const bool renamed=RenameClientProfile(alpha,quoted,error);
    if(!renamed)std::wcout<<L"Quoted rename diagnostic: source="<<(g_sDataDir/L"Sites"/alpha)<<L"; target="<<(g_sDataDir/L"Sites"/quoted)<<L"; targetLength="<<(g_sDataDir/L"Sites"/quoted).native().size()<<L"; withinManagedBudget="<<CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(g_sDataDir/L"Sites",quoted)<<L"; error="<<error<<L'\n';
    Check(renamed,"Production client rename commits the literal apostrophe-delimited new identity");
    Check(DiskList(L"pinned")==std::vector<std::wstring>{quoted,L"Beta"}&&g_pinnedClients==std::vector<std::wstring>{quoted,L"Beta"},"Renamed quoted identity matches native pinned reload and committed memory");
    Check(Preferences()==std::vector<std::wstring>{quoted+L"|edge"},"Rename keeps the quoted client's browser-qualified restore key independent");
    Check(Bytes(g_sDataDir/L"Sites"/quoted/client_notes::kFileName)=="RENAMED CLIENT ORIGINAL NOTE"&&!fs::exists(g_sDataDir/L"Sites"/alpha),"Literal quoted rename moves only the intended original note and directory");OtherIntact();
  }
}
static void RememberAndInitialCases(){
  Setup(true);NotesRememberClient(quoted);NotesRememberClient(L"Beta");
  Check(NotesRememberedClient(L"last_client")==L"Beta"&&NotesRememberedClient(L"recent1")==quoted,"Actual Notes recent-client publication retains the full literal quoted identity");
  NotesRememberClient(quoted);Check(NotesRememberedClient(L"last_client")==quoted&&NotesRecentClients()==std::vector<std::wstring>{quoted,L"Beta"},"Actual Notes last-client recall cannot resolve to the separate unquoted client");OtherIntact();
  for(unsigned action=0;action<3;++action){
    Setup(true);bool saved=false;
    if(action==0){g_pinnedClients={quoted};saved=SavePinnedClients();Check(saved&&DiskList(L"pinned")==std::vector<std::wstring>{quoted},"Initial pinned serializer round-trips literal quoted identity");}
    else if(action==1){g_archivedClients={quoted};saved=SaveArchivedClients();Check(saved&&DiskList(L"archived")==std::vector<std::wstring>{quoted},"Initial archive serializer round-trips literal quoted identity");}
    else{Seed({quoted});saved=SaveRestoreTabsPreferences();wchar_t raw[512]{};GetPrivateProfileStringW(L"restore_tabs",L"disabled_client0",L"",raw,512,g_sConfigPath.c_str());Check(saved&&raw==quoted,"Legacy unqualified restore serializer round-trips literal quoted identity");}
    OtherIntact();
  }
}
static void VariantAndFailureCases(){
  Setup(false);
  for(const auto &name:std::vector<std::wstring>{L"'",L"''",quoted,L"'Leading",L"Trailing'",L"O'Brien",L"''Edge''",L"'\u00C5\u4E2D\u6587'"}){
    AddClient(name,"VARIANT PRIVATE NOTE");
    Check(SaveClientPreferenceDelta(name,true,std::nullopt)&&DiskList(L"pinned")==std::vector<std::wstring>{L"Beta",name},"All eight native apostrophe and Unicode identity variants reload exactly");
    Check(SaveClientPreferenceDelta(name,false,std::nullopt)&&DiskList(L"pinned")==std::vector<std::wstring>{L"Beta"},"All eight identity variants unpin only their exact row");
  }
  OtherIntact();
  Setup(false);const auto before=Bytes(g_sConfigPath);const auto pins=g_pinnedClients,archives=g_archivedClients;
  HANDLE held=CreateFileW(g_sConfigPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);Check(held!=INVALID_HANDLE_VALUE,"Quoted identity fixture holds an actual native publication lock");
  const bool saved=SaveClientPreferenceDelta(quoted,true,std::nullopt);CloseHandle(held);
  Check(!saved&&Bytes(g_sConfigPath)==before&&g_pinnedClients==pins&&g_archivedClients==archives,"Failed quoted publication preserves exact original bytes and both memory lists");
  Check(SaveClientPreferenceDelta(quoted,true,std::nullopt)&&DiskList(L"pinned")==std::vector<std::wstring>{L"Beta",quoted},"Unlocked quoted publication retries against current bytes without aliasing");OtherIntact();
}
static void Run(){const auto before=scenarios;PinArchiveCases();RenameCases();RememberAndInitialCases();VariantAndFailureCases();Check(scenarios-before==14,"Quoted identity regressions cover fourteen new private lifecycle/preference scenarios");std::cout<<"Quoted client identity: 14 private scenarios; 8 name variants; canonical/fallback, rename, recent recall, initial serializers and locked publication passed.\n";}
}
