// Expected-original ordered snapshots for runtime reorder and maintenance.
static const wchar_t *SnapshotSection(unsigned mode) { return mode==3?L"restore_tabs":mode==2?L"archived":L"pinned"; }
static const wchar_t *SnapshotCount(unsigned mode) { return mode==3?L"disabled_count":L"count"; }
static const wchar_t *SnapshotPrefix(unsigned mode) { return mode==3?L"disabled_client":L"client"; }
static std::wstring SnapshotGamma(unsigned mode) { return mode==3?L"Gamma|firefox":L"Gamma"; }
static std::vector<std::wstring> SnapshotValues(unsigned mode) {
  return mode==3?Preferences():mode==2?g_archivedClients:g_pinnedClients;
}
static void AssignSnapshot(unsigned mode,const std::vector<std::wstring> &values) {
  if(mode==3)Seed(values);else if(mode==2)g_archivedClients=values;else g_pinnedClients=values;
}
static std::string SnapshotIni() {
  std::wstring text=L"; snapshot comments survive\r\n";
  for(unsigned mode:{1u,2u,3u}) {
    const auto values=SnapshotValues(mode);
    text+=L"["+std::wstring(SnapshotSection(mode))+L"]\r\n"+SnapshotCount(mode)+L"="+std::to_wstring(values.size())+L"\r\nfuture_key=SNAPSHOT KEEP\r\n";
    for(size_t i=0;i<values.size();++i)text+=std::wstring(SnapshotPrefix(mode))+std::to_wstring(i)+L"="+values[i]+L"\r\n";
    text+=L"; keep trailing section comment\r\n";
  }
  text+=L"[independent]\r\nmarker=NEWER WORK\r\n";return EncodeText(text);
}
static std::vector<std::wstring> SnapshotOriginal(unsigned mode) {
  if(mode==0)return {alpha,L"Beta"};
  if(mode==3)return {alpha+L"|edge",L"Missing|chrome"};
  return {alpha,L"Missing"};
}
static void PrepareSnapshot(unsigned mode) {
  Prepare({},{});AssignSnapshot(mode,SnapshotOriginal(mode));WriteCollections(SnapshotIni());
}
static std::vector<std::wstring> SnapshotDisk(unsigned mode) {
  WritePrivateProfileStringW(nullptr,nullptr,nullptr,g_sConfigPath.c_str());
  const UINT count=GetPrivateProfileIntW(SnapshotSection(mode),SnapshotCount(mode),0,g_sConfigPath.c_str());
  Check(count<=MAX_RESTORE_TAB_PREFERENCES,"Snapshot disk read stays bounded");
  std::vector<std::wstring> result;
  for(UINT i=0;i<count;++i) {
    wchar_t value[512]{};const auto key=std::wstring(SnapshotPrefix(mode))+std::to_wstring(i);
    GetPrivateProfileStringW(SnapshotSection(mode),key.c_str(),L"",value,512,g_sConfigPath.c_str());result.emplace_back(value);
  }
  return result;
}
static void CheckSnapshotUnknown() {
  for(unsigned mode:{1u,2u,3u}) {
    wchar_t value[64]{};GetPrivateProfileStringW(SnapshotSection(mode),L"future_key",L"",value,64,g_sConfigPath.c_str());
    Check(std::wstring(value)==L"SNAPSHOT KEEP","Whole-list save preserves unknown keys in every collection section");
  }
}
static void ApplySnapshot(unsigned mode) {
  if(mode==0) {
    g_pinnedClientsBeforeDrag=g_pinnedClients;g_bPinnedDragSnapshotValid=true;g_bPinnedDragCancelled=false;
    if(g_pinnedClients.size()>1)std::swap(g_pinnedClients[0],g_pinnedClients[1]);
    g_pinnedClientsExpectedDuringDrag=g_pinnedClients;FinishPinnedDragPersistence(true);
  } else if(mode==1)PrunePinnedClients();
  else if(mode==2)PruneArchivedClients();
  else PruneRestoreTabsPreferences();
}
static std::vector<std::wstring> SnapshotDesired(unsigned mode,std::vector<std::wstring> values) {
  if(mode==0)std::swap(values[0],values[1]);
  else values.erase(std::remove_if(values.begin(),values.end(),[](const auto &value){return value==L"Missing"||value==L"Missing|chrome";}),values.end());
  return values;
}
static bool SaveSnapshot(unsigned mode,const std::vector<std::wstring> &expected,ClientPreferenceSnapshot &observed) {
  if(mode==3)return SaveRestoreTabsPreferences(&expected,&observed);
  if(mode==2)return SaveArchivedClients(&expected,&observed);
  return SavePinnedClients(&expected,&observed);
}
static void SnapshotConflictCases() {
  for(unsigned mode:{0u,1u,2u,3u})for(bool concurrent:{false,true}) {
    PrepareSnapshot(mode);const auto original=SnapshotValues(mode);auto newer=original;newer.push_back(SnapshotGamma(mode));
    AssignSnapshot(mode,newer);CollectionConflict attack{SnapshotIni()};AssignSnapshot(mode,original);
    const auto beforeNotices=notices;
    if(concurrent)config_persistence::SetIniPublicationHookForTest(ReplaceCollections,&attack);
    else WriteCollections(attack.bytes);
    ApplySnapshot(mode);config_persistence::SetIniPublicationHookForTest(nullptr,nullptr);
    if(concurrent)Check(attack.attempted&&attack.changed,"Whole-list action reaches actual competing publication boundary");
    Check(Bytes(g_sConfigPath)==attack.bytes&&notices==beforeNotices+1,"Stale whole-list action preserves exact newer saved bytes and reports once");
    Check(SnapshotValues(mode)==(concurrent?original:newer),"Only validated staged mismatch refreshes memory; publication I/O conflict restores original");
    if(concurrent) {
      const auto retryNotices=notices;ApplySnapshot(mode);
      Check(notices==retryNotices+1&&SnapshotValues(mode)==newer&&Bytes(g_sConfigPath)==attack.bytes,"Immediate retry refuses stale expected snapshot and refreshes the observed list without recursion");
    }
    const auto retryNotices=notices;ApplySnapshot(mode);const auto wanted=SnapshotDesired(mode,newer);
    Check(notices==retryNotices&&SnapshotValues(mode)==wanted&&SnapshotDisk(mode)==wanted,"Fresh retry applies intended reorder/prune and preserves newer Gamma membership");CheckSnapshotUnknown();
  }
}
static void SnapshotMalformedAndIo() {
  for(unsigned mode:{0u,1u,2u,3u}) {
    const auto prefix=std::wstring(SnapshotPrefix(mode));
    const std::wstring first=mode==3?alpha+L"|edge":alpha;
    const std::vector<std::wstring> malformed{L"",L"invalid",L"2049",L"2\r\n"+prefix+L"0="+first,L"1\r\n"+prefix+L"0="+(mode==3?L"Alpha|unsupported":L"CON"),L"2\r\n"+prefix+L"0="+first+L"\r\n"+prefix+L"1="+first};
    for(const auto &bad:malformed) {
      PrepareSnapshot(mode);const auto original=SnapshotValues(mode);
      const auto bytes=EncodeText(L"["+std::wstring(SnapshotSection(mode))+L"]\r\n"+SnapshotCount(mode)+L"="+bad+L"\r\nfuture_key=KEPT\r\n");WriteCollections(bytes);
      const auto beforeNotices=notices;ApplySnapshot(mode);
      Check(notices==beforeNotices+1&&SnapshotValues(mode)==original&&Bytes(g_sConfigPath)==bytes,"Malformed snapshot count/row never supplies partial observed memory or changes live bytes");
    }
    for(bool nativeLock:{true,false}) {
      PrepareSnapshot(mode);const auto original=SnapshotValues(mode);const auto before=Bytes(g_sConfigPath);const auto beforeNotices=notices;
      HANDLE lock=INVALID_HANDLE_VALUE;
      if(nativeLock) {lock=CreateFileW(g_sConfigPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);Check(lock!=INVALID_HANDLE_VALUE,"Private whole-list config lock acquired");}
      else {failedDirectWriteAt=directCollectionWrites+1;shortDirectWrite=mode%2==0;}
      ApplySnapshot(mode);failedDirectWriteAt=0;if(lock!=INVALID_HANDLE_VALUE)CloseHandle(lock);
      Check(notices==beforeNotices+1&&SnapshotValues(mode)==original&&Bytes(g_sConfigPath)==before,"Whole-list native lock/partial-write/disk-full failure restores original memory and exact live bytes");
    }
  }
}
static void SnapshotZeroAndIdentity() {
  for(unsigned mode:{0u,1u,2u,3u}) {
    PrepareSnapshot(mode);const auto original=SnapshotValues(mode);
    AssignSnapshot(mode,{});const auto zero=SnapshotIni();AssignSnapshot(mode,original);WriteCollections(zero);
    const auto beforeNotices=notices;ApplySnapshot(mode);
    Check(notices==beforeNotices+1&&SnapshotValues(mode).empty()&&Bytes(g_sConfigPath)==zero,"Validated zero snapshot is engaged and refreshes stale memory without overwriting disk");
  }
  for(unsigned mode:{1u,2u,3u}) {
    PrepareSnapshot(mode);AssignSnapshot(mode,{});WriteCollections(SnapshotIni());
    const std::vector<std::wstring> empty,requested{mode==3?alpha+L"|edge":alpha};AssignSnapshot(mode,requested);
    ClientPreferenceSnapshot observed=std::vector<std::wstring>{L"stale output"};
    Check(SaveSnapshot(mode,empty,observed)&&!observed&&SnapshotDisk(mode)==requested,"True empty original permits expected whole-list serialization and clears stale observed output");CheckSnapshotUnknown();
    PrepareSnapshot(mode);const auto original=SnapshotValues(mode);const auto savedPath=g_sConfigPath;g_sConfigPath.clear();observed=std::vector<std::wstring>{L"stale output"};
    Check(!SaveSnapshot(mode,original,observed)&&!observed&&SnapshotValues(mode)==original,"Empty config-path failure resets observed output without altering original memory");g_sConfigPath=savedPath;
  }
  PrepareSnapshot(0);auto current=g_pinnedClients;std::swap(current[0],current[1]);AssignSnapshot(0,current);const auto ordered=SnapshotIni();AssignSnapshot(0,SnapshotOriginal(0));WriteCollections(ordered);
  const auto beforeNotices=notices;ApplySnapshot(0);
  Check(notices==beforeNotices+1&&g_pinnedClients==current&&Bytes(g_sConfigPath)==ordered,"Exact expected order is checked even when desired order already happens to match newer saved order");
  PrepareSnapshot(1);auto casing=g_pinnedClients;casing[0][0]=L'a';AssignSnapshot(1,casing);const auto renamed=SnapshotIni();AssignSnapshot(1,SnapshotOriginal(1));WriteCollections(renamed);
  ApplySnapshot(1);Check(g_pinnedClients==casing&&Bytes(g_sConfigPath)==renamed,"Expected snapshot comparison notices exact spelling changes and retains newer bytes");
}
static void SnapshotRestoreMaximum() {
  PrepareSnapshot(3);auto full=Names(MAX_RESTORE_TAB_PREFERENCES);for(auto &name:full)name+=L"|edge";
  Seed(full);WriteCollections(SnapshotIni());const auto original=full;full.erase(full.begin());full.push_back(alpha+L"|firefox");Seed(full);
  ClientPreferenceSnapshot observed;const auto started=GetTickCount64();const auto directBefore=directCollectionWrites,indexedBefore=indexedCollectionWrites,countBefore=collectionCountWrites;
  Check(SaveRestoreTabsPreferences(&original,&observed)&&!observed,"Maximum Restore-tabs snapshot commits against exact current original");
  Check(Preferences()==full&&SnapshotDisk(3)==full,"Maximum Restore-tabs result reloads all ordered qualified keys");
  Check(directCollectionWrites==directBefore+1&&indexedCollectionWrites==indexedBefore&&collectionCountWrites==countBefore,"Restore-tabs maximum uses one staged direct write and no per-row native API writes");CheckSnapshotUnknown();
  std::cout<<"Restore snapshot maximum: entries="<<MAX_RESTORE_TAB_PREFERENCES<<", direct_writes="<<directCollectionWrites-directBefore<<", milliseconds="<<GetTickCount64()-started<<std::endl;
}
static void SnapshotUnsavedDragPrune() {
  for(bool changedOnDisk:{false,true}) {
    Prepare({},{});g_pinnedClients={L"Missing",L"Beta",L"Gamma"};WriteCollections(SnapshotIni());
    const auto original=g_pinnedClients;g_mainDragKind=MainDragKind::PinnedClient;
    g_pinnedClientsBeforeDrag=original;g_bPinnedDragSnapshotValid=true;g_bPinnedDragCancelled=false;
    g_pinnedClients={L"Beta",L"Gamma",L"Missing"};g_pinnedClientsExpectedDuringDrag=g_pinnedClients;
    std::vector<std::wstring> newer=original;newer.push_back(alpha);
    if(changedOnDisk) {const auto unsaved=g_pinnedClients;g_pinnedClients=newer;WriteCollections(SnapshotIni());g_pinnedClients=unsaved;}
    const auto beforeNotices=notices;PrunePinnedClients();
    const std::vector<std::wstring> wanted=changedOnDisk?newer:std::vector<std::wstring>{L"Beta",L"Gamma"};
    Check(g_pinnedClients==wanted&&SnapshotDisk(1)==wanted,"Prune recognizes its own unsaved drag baseline while retaining independently newer membership");
    Check(notices==beforeNotices+(changedOnDisk?1u:0u),"Only an external disk change refuses unsaved-drag pruning");
    const auto committed=Bytes(g_sConfigPath);FinishPinnedDragPersistence(true);g_mainDragKind=MainDragKind::None;
    Check(g_pinnedClients==wanted&&Bytes(g_sConfigPath)==committed,"Independent prune or conflict refresh cancels stale drag persistence without restoring removed clients");CheckSnapshotUnknown();
  }
}
static void SnapshotMaintenanceCases() {
  SnapshotConflictCases();SnapshotMalformedAndIo();SnapshotZeroAndIdentity();SnapshotRestoreMaximum();SnapshotUnsavedDragPrune();
}
