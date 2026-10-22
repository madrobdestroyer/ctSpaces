// Current staged client collections; no app entry, HWND, browser or desktop input.
#include <windows.h>
#include "RetainedFixture.h"
#include <tlhelp32.h>
static HANDLE WINAPI CollectionSnapshot(DWORD,DWORD) { return CreateEventW(nullptr,FALSE,FALSE,nullptr); }
static BOOL WINAPI CollectionFirst(HANDLE,LPPROCESSENTRY32W item) {
  wcscpy_s(item->szExeFile,L"private-collection-fixture.exe");
  item->th32ProcessID=GetCurrentProcessId(); return TRUE;
}
static BOOL WINAPI CollectionNext(HANDLE,LPPROCESSENTRY32W) { SetLastError(ERROR_NO_MORE_FILES); return FALSE; }
static unsigned indexedCollectionWrites=0,collectionCountWrites=0;
static unsigned directCollectionWrites=0,failedDirectWriteAt=0;
static bool shortDirectWrite=false;
BOOL CtSpacesClientPreferenceStageWrite(HANDLE file,const void *bytes,DWORD count,DWORD *written) {
  ++directCollectionWrites;
  if(failedDirectWriteAt==directCollectionWrites) {
    if(shortDirectWrite)return ::WriteFile(file,bytes,count/2,written,nullptr);
    *written=0;SetLastError(ERROR_DISK_FULL);return FALSE;
  }
  return ::WriteFile(file,bytes,count,written,nullptr);
}
static BOOL WINAPI CountedCollectionWrite(LPCWSTR section,LPCWSTR key,LPCWSTR value,LPCWSTR path) {
  if(section&&key) {
    if(!_wcsicmp(section,L"pinned")||!_wcsicmp(section,L"archived")) {
      if(!wcsncmp(key,L"client",6))++indexedCollectionWrites;
      if(!wcscmp(key,L"count"))++collectionCountWrites;
    } else if(!_wcsicmp(section,L"restore_tabs")) {
      if(!wcsncmp(key,L"disabled_client",15))++indexedCollectionWrites;
      if(!wcscmp(key,L"disabled_count"))++collectionCountWrites;
    }
  }
  return ::WritePrivateProfileStringW(section,key,value,path);
}
#define WritePrivateProfileStringW CountedCollectionWrite
#define CreateToolhelp32Snapshot CollectionSnapshot
#define Process32FirstW CollectionFirst
#define Process32NextW CollectionNext
#define wmain OtherBrowserTestEntryNotRun
#include "BrowserLaunchFailureTests.cpp"
#undef wmain

static unsigned scenarios=0;
static fs::path base;
static const std::wstring alpha=L"Alpha \u00C5ngstr\u00F6m \u4E2D\u6587";
static std::string EncodeText(const std::wstring &text) {
  std::string bytes("\xFF\xFE",2);
  bytes.append(reinterpret_cast<const char *>(text.data()),text.size()*sizeof(wchar_t));return bytes;
}
static std::string EncodeCollections(const std::vector<std::wstring> &pinned,
                                     const std::vector<std::wstring> &archived) {
  std::wstring text;
  for (bool archive : {false,true}) {
    const auto &values=archive?archived:pinned;
    text+=archive?L"[archived]\r\n":L"[pinned]\r\n";
    text+=L"count="+std::to_wstring(values.size())+L"\r\nfuture_key=KEEP NEWER FIELD\r\n";
    for(size_t i=0;i<values.size();++i)
      text+=L"client"+std::to_wstring(i)+L"="+values[i]+L"\r\n";
  }
  text+=L"[independent]\r\nmarker=NEWER WORK\r\n";
  std::string bytes("\xFF\xFE",2);
  bytes.append(reinterpret_cast<const char *>(text.data()),text.size()*sizeof(wchar_t));
  return bytes;
}
static void WriteCollections(const std::string &bytes) {
  std::ofstream writer(g_sConfigPath,std::ios::binary|std::ios::trunc);
  writer<<bytes;writer.close();Check(static_cast<bool>(writer),"Private collection fixture writes");
  WritePrivateProfileStringW(nullptr,nullptr,nullptr,g_sConfigPath.c_str());
}
static void Prepare(const std::vector<std::wstring> &pinned,
                    const std::vector<std::wstring> &archived) {
  g_sDataDir=base/std::to_wstring(++scenarios);g_sConfigPath=g_sDataDir/L"config.ini";
  for(const auto &name:{alpha,std::wstring(L"Beta"),std::wstring(L"Gamma"),std::wstring(L"Delta")}) {
    fs::create_directories(g_sDataDir/L"Sites"/name);
    std::ofstream marker(g_sDataDir/L"Sites"/name/kClientSchemaMarkerName,std::ios::binary);
    marker<<kClientSchemaMarkerText;Check(static_cast<bool>(marker),"Private client schema marker writes");
  }
  g_pinnedClients=pinned;g_archivedClients=archived;Seed({});
  g_sessions={{alpha,BrowserKind::Edge,0x7ffff678}};g_iSelectedSessionTab=1;
  WriteCollections(EncodeCollections(pinned,archived));
}
static std::vector<std::wstring> DiskList(const wchar_t *section) {
  WritePrivateProfileStringW(nullptr,nullptr,nullptr,g_sConfigPath.c_str());
  const auto count=GetPrivateProfileIntW(section,L"count",0,g_sConfigPath.c_str());
  Check(count<=MAX_ARCHIVED_CLIENTS,"Saved collection read stays bounded");
  std::vector<std::wstring> result;
  for(UINT i=0;i<count;++i) {
    wchar_t value[256]{};
    GetPrivateProfileStringW(section,(L"client"+std::to_wstring(i)).c_str(),L"",value,256,g_sConfigPath.c_str());
    result.emplace_back(value);
  }
  return result;
}
static void CheckUnknown() {
  for(const auto *section:{L"pinned",L"archived",L"independent"}) {
    wchar_t value[64]{};
    GetPrivateProfileStringW(section,section==std::wstring(L"independent")?L"marker":L"future_key",L"",value,64,g_sConfigPath.c_str());
    Check(std::wstring(value)==(section==std::wstring(L"independent")?L"NEWER WORK":L"KEEP NEWER FIELD"),"Unknown collection keys and independent section survive");
  }
}
struct CollectionConflict { std::string bytes; bool attempted=false,changed=false; };
static void ReplaceCollections(const fs::path &live,const fs::path &,
    config_persistence::IniPublicationBoundary boundary,void *context) {
  if(boundary!=config_persistence::IniPublicationBoundary::BeforePublish)return;
  auto &attack=*static_cast<CollectionConflict *>(context);attack.attempted=true;
  const auto other=live.parent_path()/L"competing-settings.ini";
  {std::ofstream writer(other,std::ios::binary|std::ios::trunc);writer<<attack.bytes;if(!writer)return;}
  attack.changed=ReplaceFileW(live.c_str(),other.c_str(),nullptr,REPLACEFILE_IGNORE_MERGE_ERRORS,nullptr,nullptr)!=FALSE;
  // A retained command must not switch target when selection changes mid-save.
  g_sessions={{L"Delta",BrowserKind::Firefox,0x7ffff679}};g_iSelectedSessionTab=1;
}
static void RetryAndPreclick() {
  for(bool archive:{false,true}) for(bool conflict:{true,false}) {
    const std::vector<std::wstring> initialPinned=archive?std::vector<std::wstring>{}:std::vector<std::wstring>{L"Beta"};
    const std::vector<std::wstring> initialArchived=archive?std::vector<std::wstring>{alpha,L"Beta"}:std::vector<std::wstring>{};
    Prepare(initialPinned,initialArchived);
    auto newerPinned=initialPinned,newerArchived=initialArchived;
    (archive?newerArchived:newerPinned).push_back(L"Gamma");
    CollectionConflict attack{EncodeCollections(newerPinned,newerArchived)};
    if(conflict) {
      config_persistence::SetIniPublicationHookForTest(ReplaceCollections,&attack);
      const auto beforeNotices=notices;std::wstring status;
      if(archive)Check(!RestoreArchivedClient(alpha,&status),"Restore rejects conflicting publication");
      else ToggleSelectedClientPin();
      config_persistence::SetIniPublicationHookForTest(nullptr,nullptr);
      Check(attack.attempted&&attack.changed&&notices==beforeNotices+1,"Conflicting native publication is preserved and reported once");
      Check(g_pinnedClients==initialPinned&&g_archivedClients==initialArchived&&Bytes(g_sConfigPath)==attack.bytes,"Conflict changes neither collection memory nor competing saved bytes");
    } else WriteCollections(attack.bytes);
    g_sessions={{alpha,BrowserKind::Edge,0x7ffff678}};g_iSelectedSessionTab=1;std::wstring status;
    if(archive) {
      Check(RestoreArchivedClient(alpha,&status),"Immediate retry/pre-click restore succeeds");
      newerArchived.erase(newerArchived.begin());
      Check(g_archivedClients==newerArchived,"Restore retains unrelated newer collection entries");
    } else {
      ToggleSelectedClientPin();newerPinned.push_back(alpha);
      Check(g_pinnedClients==newerPinned,"Pin retains unrelated newer collection entries");
    }
    Check(DiskList(L"pinned")==newerPinned&&DiskList(L"archived")==newerArchived,"Committed current lists survive native reload");CheckUnknown();
  }
}
static void ArchiveAddsAtomically() {
  for(bool conflict:{true,false}) {
    const std::vector<std::wstring> initialPinned{alpha,L"Beta"},initialArchived{L"Beta"};
    Prepare(initialPinned,initialArchived);
    const std::vector<std::wstring> newerPinned{alpha,L"Delta"},newerArchived{L"Beta",L"Gamma"};
    CollectionConflict attack{EncodeCollections(newerPinned,newerArchived)};std::wstring status;
    if(conflict) {
      config_persistence::SetIniPublicationHookForTest(ReplaceCollections,&attack);
      const auto beforeNotices=notices;
      Check(!ArchiveClientProfile(alpha,status),"Archive add rejects competing publication");
      config_persistence::SetIniPublicationHookForTest(nullptr,nullptr);
      if(!attack.attempted)std::wcerr<<L"Archive preflight status: "<<status<<L'\n';
      Check(attack.attempted&&attack.changed&&notices==beforeNotices+1,"Archive conflict reaches actual publisher and reports once");
      Check(g_pinnedClients==initialPinned&&g_archivedClients==initialArchived&&Bytes(g_sConfigPath)==attack.bytes,"Failed archive keeps both original memory lists and newer saved bytes");
    } else WriteCollections(attack.bytes);
    Check(ArchiveClientProfile(alpha,status),"Archive add publishes against current pinned and archived lists");
    const std::vector<std::wstring> wantedArchived{L"Beta",L"Gamma",alpha},wantedPinned{L"Delta"};
    Check(g_pinnedClients==wantedPinned&&g_archivedClients==wantedArchived,"Archive commits its target in both lists after publication");
    Check(DiskList(L"pinned")==wantedPinned&&DiskList(L"archived")==wantedArchived,"Archive persists add plus unpin without dropping newer clients");CheckUnknown();
  }
}
static std::vector<std::wstring> Names(size_t count) {
  std::vector<std::wstring> names;for(size_t i=0;i<count;++i)names.push_back(L"Client "+std::to_wstring(i));return names;
}
static void BoundsAndFailure() {
  for(bool archive:{false,true}) {
    const size_t maximum=archive?MAX_ARCHIVED_CLIENTS:MAX_PINNED_CLIENTS;
    Prepare({L"Beta"},{L"Gamma"});auto full=Names(maximum);
    const auto started=GetTickCount64();const auto initialWrites=indexedCollectionWrites,initialCounts=collectionCountWrites,initialDirect=directCollectionWrites;
    std::cout<<"Boundary collection start: "<<(archive?"archived":"pinned")<<", maximum="<<maximum<<std::endl;
    WriteCollections(EncodeCollections(archive?std::vector<std::wstring>{L"Beta"}:full,archive?full:std::vector<std::wstring>{L"Gamma"}));
    const auto before=Bytes(g_sConfigPath);
    const auto oldPinned=g_pinnedClients,oldArchived=g_archivedClients;
    auto beforeNotices=notices;
    Check(!SaveClientPreferenceDelta(alpha,archive?std::optional<bool>{}:true,archive?true:std::optional<bool>{}),"Full collection refuses additional target");
    Check(notices==beforeNotices+1&&g_pinnedClients==oldPinned&&g_archivedClients==oldArchived&&Bytes(g_sConfigPath)==before,"Full-list failure changes no memory or bytes");
    Check(SaveClientPreferenceDelta(full.front(),archive?std::optional<bool>{}:false,archive?false:std::optional<bool>{}),"Full collection permits target removal");
    full.erase(full.begin());
    Check(DiskList(archive?L"archived":L"pinned")==full,"Full-list removal retains all other ordered rows");
    Check(SaveClientPreferenceDelta(alpha,archive?std::optional<bool>{}:true,archive?true:std::optional<bool>{}),"One free slot permits target add");full.push_back(alpha);
    Check(DiskList(archive?L"archived":L"pinned")==full,"Boundary add retains all ordered current rows");
    const auto order=full;
    Check(SaveClientPreferenceDelta(full.front(),archive?std::optional<bool>{}:true,archive?true:std::optional<bool>{}),"Already-present target is accepted at full capacity");
    Check(DiskList(archive?L"archived":L"pinned")==order,"Idempotent add preserves target position");CheckUnknown();
    Check(indexedCollectionWrites==initialWrites&&collectionCountWrites==initialCounts&&directCollectionWrites-initialDirect==2,"Canonical boundary uses two full stage writes and no per-row INI writes");
    std::cout<<"Boundary collection complete: "<<(archive?"archived":"pinned")<<", indexed_writes="<<indexedCollectionWrites-initialWrites<<", count_writes="<<collectionCountWrites-initialCounts<<", direct_writes="<<directCollectionWrites-initialDirect<<", milliseconds="<<GetTickCount64()-started<<std::endl;
  }
  Prepare({L"Beta"},{L"Gamma"});const auto before=Bytes(g_sConfigPath);const auto oldPinned=g_pinnedClients,oldArchived=g_archivedClients;
  HANDLE lock=CreateFileW(g_sConfigPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  Check(lock!=INVALID_HANDLE_VALUE,"Private settings lock acquired");const auto beforeNotices=notices;
  const bool saved=SaveClientPreferenceDelta(alpha,false,true);CloseHandle(lock);
  Check(!saved&&notices==beforeNotices+1&&g_pinnedClients==oldPinned&&g_archivedClients==oldArchived&&Bytes(g_sConfigPath)==before,"Native sharing failure keeps both memory lists and exact bytes");
}
static void MalformedLists() {
  for(bool archive:{false,true}) {
    const std::wstring section=archive?L"archived":L"pinned";
    for(const auto &bad:{std::wstring(L""),std::wstring(L"-1"),std::wstring(L"+1"),std::wstring(L"invalid"),std::wstring(L"42949672960"),std::to_wstring(archive?2049:9),std::wstring(L"1"),std::wstring(L"1\r\nclient0="),std::wstring(L"1\r\nclient0=")+std::wstring(241,L'x'),std::wstring(L"1\r\nclient0=CON"),std::wstring(L"1\r\nclient0=Bad/Name"),std::wstring(L"2\r\nclient0=Beta\r\nclient1=bETA")}) {
      Prepare({L"Beta"},{L"Gamma"});
      std::wstring text=L"["+section+L"]\r\ncount="+bad+L"\r\nfuture_key=KEEP\r\n";
      std::string bytes("\xFF\xFE",2);bytes.append(reinterpret_cast<const char *>(text.data()),text.size()*sizeof(wchar_t));WriteCollections(bytes);
      const auto beforeNotices=notices;const auto oldPinned=g_pinnedClients,oldArchived=g_archivedClients;
      Check(!SaveClientPreferenceDelta(alpha,archive?std::optional<bool>{}:true,archive?false:std::optional<bool>{}),"Malformed collection count or row is rejected");
      Check(notices==beforeNotices+1&&g_pinnedClients==oldPinned&&g_archivedClients==oldArchived&&Bytes(g_sConfigPath)==bytes,"Malformed collection rejection preserves memory and exact bytes");
    }
  }
  Prepare({L"Beta"},{L"Gamma"});
  auto bytes=EncodeCollections({L"Beta"},{L"Gamma"});
  // The archive stage is edited first. An invalid pinned list must discard both edits.
  const std::wstring text=L"[archived]\r\ncount=1\r\nclient0=Gamma\r\n[pinned]\r\ncount=1\r\n";
  bytes.assign("\xFF\xFE",2);bytes.append(reinterpret_cast<const char *>(text.data()),text.size()*sizeof(wchar_t));WriteCollections(bytes);
  const auto oldPinned=g_pinnedClients,oldArchived=g_archivedClients;
  Check(!SaveClientPreferenceDelta(alpha,false,true),"Invalid second collection aborts paired publication");
  Check(g_pinnedClients==oldPinned&&g_archivedClients==oldArchived&&Bytes(g_sConfigPath)==bytes,"Paired staging never partially commits memory or saved lists");
}
static void CanonicalBytesAndFallback() {
  Prepare({L"Beta",L"Gamma"},{L"Delta"});
  const std::wstring before=L"; leading comment stays exact\r\n[before]\r\nodd_key = \u00C5 \u4E2D\u6587\r\n\r\n[pinned]\r\n; count comment\r\ncount=2\r\nfuture_key = keep  spaces\r\nclient0=Beta\r\n; middle comment\r\nclient1=Gamma\r\n# preserved unknown line\r\n\r\n[archived]\r\ncount=1\r\nclient0=Delta\r\n[after]\r\nfuture=after both lists\r\n; final comment\r\n";
  WriteCollections(EncodeText(before));const auto firstDirect=directCollectionWrites;
  Check(SaveClientPreferenceDelta(L"Beta",false,std::nullopt),"Canonical removal with unknown lines commits");
  std::wstring expected=before;
  expected.replace(expected.find(L"count=2\r\n"),9,L"count=1\r\n");
  expected.replace(expected.find(L"client0=Beta\r\n"),14,L"client0=Gamma\r\n");
  expected.erase(expected.find(L"client1=Gamma\r\n"),15);
  Check(Bytes(g_sConfigPath)==EncodeText(expected),"Removal changes only owned canonical lines; comments, unknown fields and other sections remain byte exact");
  Check(SaveClientPreferenceDelta(alpha,true,std::nullopt),"Canonical append with unknown trailing bytes commits");
  expected.replace(expected.find(L"count=1\r\n"),9,L"count=2\r\n");
  expected.insert(expected.find(L"[archived]"),L"client1="+alpha+L"\r\n");
  Check(Bytes(g_sConfigPath)==EncodeText(expected)&&directCollectionWrites-firstDirect==2,"Append preserves all unknown bytes and changes one staged file per operation");
  Check(DiskList(L"pinned")==std::vector<std::wstring>{L"Gamma",alpha},"Native INI reload observes canonical direct-write results");

  const std::vector<std::wstring> formats{
    L"[PINNED]\r\ncount=1\r\nclient0=Beta\r\n",
    L"[pinned]\r\ncount = 1\r\nclient0 = Beta\r\nfuture_key=KEEP NEWER FIELD\r\n",
    L"[pinned]\r\ncount=0001\r\nclient0=Beta\r\n",
    L"[pinned]\ncount=1\nclient0=Beta\n",
    L"[pinned]\r\ncount=1\r\nclient0=Beta",
    L"[pinned]\r\ncount=1\r\nclient0=Beta\r\nclient01=Other\r\n",
    L"[pinned]\r\ncount=1\r\nclient0=Beta\r\nclient0=Other\r\n",
    L"[pinned]\r\ncount=1\r\nclient0=Beta\r\n[pinned]\r\ncount=1\r\nclient0=Other\r\n",
    L"[pinned]\r\ncount=1\r\nclient0=Beta\r\n\v[pinned]\r\ncount=1\r\nclient0=Other\r\n",
    L"[pinned]\r\ncount=1\r\nclient0=Beta\r\n\f[pinned]\r\ncount=1\r\nclient0=Other\r\n"
  };
  for(size_t index=0;index<formats.size();++index) {
    Prepare({L"Beta"},{L"Gamma"});const auto bytes=EncodeText(formats[index]);WriteCollections(bytes);
    const auto writesBefore=directCollectionWrites;std::wstring error;
    Check(TryRewriteCanonicalClientListStage(g_sConfigPath,L"pinned",{L"Beta"},{L"Gamma"},error)==ClientListStageRewrite::NotApplicable,"Noncanonical or ambiguous formatting declines direct surgery");
    Check(Bytes(g_sConfigPath)==bytes&&directCollectionWrites==writesBefore,"Declined fast path leaves exact staged bytes untouched");
    if(index==1) {
      const auto oldIndexed=indexedCollectionWrites,oldCounts=collectionCountWrites;
      Check(SaveClientPreferenceDelta(alpha,true,std::nullopt),"Valid noncanonical formatting uses existing WinINI fallback");
      Check(DiskList(L"pinned")==std::vector<std::wstring>{L"Beta",alpha}&&directCollectionWrites==writesBefore&&indexedCollectionWrites==oldIndexed+1&&collectionCountWrites==oldCounts+1,"Fallback uses only changed native INI rows and retains current values");
      wchar_t value[64]{};GetPrivateProfileStringW(L"pinned",L"future_key",L"",value,64,g_sConfigPath.c_str());
      Check(std::wstring(value)==L"KEEP NEWER FIELD","Native fallback preserves unknown collection key");
    }
  }
}
static void PartialStageFailures() {
  for(bool shortWrite:{false,true})for(unsigned failAt:{1u,2u}) {
    Prepare({alpha,L"Beta"},{L"Gamma"});const auto before=Bytes(g_sConfigPath);
    const auto oldPinned=g_pinnedClients,oldArchived=g_archivedClients;
    const auto oldIndexed=indexedCollectionWrites,oldCounts=collectionCountWrites,oldDirect=directCollectionWrites,oldNotices=notices;
    shortDirectWrite=shortWrite;failedDirectWriteAt=directCollectionWrites+failAt;
    const bool saved=SaveClientPreferenceDelta(alpha,false,true);failedDirectWriteAt=0;
    Check(!saved&&directCollectionWrites-oldDirect==failAt,"Short write or disk-full failure aborts exactly its staged write");
    Check(Bytes(g_sConfigPath)==before&&g_pinnedClients==oldPinned&&g_archivedClients==oldArchived,"Partial first or second stage write changes neither live settings nor collection memory");
    Check(notices==oldNotices+1&&indexedCollectionWrites==oldIndexed&&collectionCountWrites==oldCounts,"Failed fast write reports once and never retries through native INI fallback");
  }
}
#include "ClientPreferenceSnapshotTests.inl"
#include "RenamePreferenceDeltaTests.inl"
#include "ClientPreferenceQuotedTests.inl"
int wmain(int argc,wchar_t **argv) {
  try {
    base=retained_fixture::CreateRoot(argc>1?fs::path(argv[1]):fs::current_path()/L"build/tests/ClientPreferenceDelta/fixtures",L"collections");
    Check(fs::is_directory(base)&&fs::is_empty(base),"Private native fixture root is freshly allocated and empty");
    RetryAndPreclick();ArchiveAddsAtomically();BoundsAndFailure();MalformedLists();CanonicalBytesAndFallback();PartialStageFailures();
    SnapshotMaintenanceCases();
    rename_preference_delta::Run();
    quoted_client_preferences::Run();
    Check(!g_hGui&&!g_hClientEdit&&!g_hComboClient&&!g_hBtnPin&&!g_hNotesWindow,"Native fixture creates no HWNDs");
    Check(launchRequests==0&&presentationRequests==0&&forbiddenPaint==0&&closeTargets.empty(),"No browser, input, foreground, repaint or process-close requests");
    std::cout<<"Client preference delta: "<<checks<<" checks, "<<scenarios<<" private scenarios, 0 failures; actual HWND/input/foreground/browser=0.\n";return 0;
  } catch(const std::exception &error) {
    config_persistence::SetIniPublicationHookForTest(nullptr,nullptr);
    std::cerr<<"Client preference delta failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;
  }
}
