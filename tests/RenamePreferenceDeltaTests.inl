// Production rename against current, private settings. The enclosing fixture
// blocks desktop presentation, process launches and unsafe painting. Keep this
// group's data root short enough for the production browser path budget.
namespace rename_preference_delta {
static const std::wstring renamed = L"Renamed \u00C5\u4E2D\u6587";
static const std::wstring extraArchived = L"Archive Other";
static std::string Settings(const std::vector<std::wstring> &pins,
                            const std::vector<std::wstring> &archives,
                            const std::vector<std::wstring> &restore) {
  std::string bytes = EncodeCollections(pins, archives);
  std::wstring section = L"; exact comment before restore \u00C5 \u4E2D\u6587\r\n[restore_tabs]\r\ndisabled_count=" +
      std::to_wstring(restore.size()) + L"\r\nfuture_key = KEEP  RESTORE  SPACES\r\n";
  for (size_t i = 0; i < restore.size(); ++i)
    section += L"disabled_client" + std::to_wstring(i) + L"=" + restore[i] + L"\r\n";
  section += L"; exact trailing comment\r\n[after_rename]\r\nunknown = untouched \u4E2D\u6587\r\n";
  bytes.append(reinterpret_cast<const char *>(section.data()), section.size() * sizeof(wchar_t));
  return bytes;
}
static void Setup(const std::vector<std::wstring> &pins,
                  const std::vector<std::wstring> &archives,
                  const std::vector<std::wstring> &restore) {
  Prepare(pins, archives); Seed(restore);
  const auto extra = g_sDataDir / L"Sites" / extraArchived;
  fs::create_directories(extra);
  std::ofstream marker(extra / kClientSchemaMarkerName, std::ios::binary);
  marker << kClientSchemaMarkerText; marker.close();
  g_sShortcutDesktopOverride = g_sDataDir / L"empty-shortcuts";
  fs::create_directories(g_sShortcutDesktopOverride);
  Check(fs::is_empty(g_sShortcutDesktopOverride), "Rename uses only its empty private shortcut directory");
  g_sessions.clear(); g_activeProfiles.clear();
  for (const auto &name : {alpha, std::wstring(L"Gamma")}) {
    std::ofstream note(g_sDataDir / L"Sites" / name / L"rename-note.txt", std::ios::binary);
    note << "EXACT PRIVATE NOTE"; note.close();
    Check(static_cast<bool>(note), "Private rename note sentinel writes");
  }
  WriteCollections(Settings(pins, archives, restore));
}
static void NotesIntact(bool committed) {
  const auto source = g_sDataDir / L"Sites" / (committed ? renamed : alpha);
  Check(Bytes(source / L"rename-note.txt") == "EXACT PRIVATE NOTE" &&
      Bytes(g_sDataDir / L"Sites/Gamma/rename-note.txt") == "EXACT PRIVATE NOTE",
      "Rename/rollback preserves selected and unrelated client note bytes");
  Check(!fs::exists(g_sDataDir / L"Sites" / (committed ? alpha : renamed)),
      "Rename outcome leaves exactly the intended original or committed directory");
}
static void ExpectSaved(const std::vector<std::wstring> &pins,
                        const std::vector<std::wstring> &archives,
                        const std::vector<std::wstring> &restore) {
  Check(g_pinnedClients == pins && g_archivedClients == archives && Preferences() == restore,
      "Rename publishes all three current preference vectors to memory");
  Check(Bytes(g_sConfigPath) == Settings(pins, archives, restore),
      "Rename preserves every unknown key, comment, space and other section byte");
  Check(DiskList(L"pinned") == pins && DiskList(L"archived") == archives,
      "Renamed current pinned and archived lists survive native reload");
  NotesIntact(true);
}
static void PreclickAndEmpty() {
  Setup({alpha,L"Beta"}, {L"Delta"}, {alpha+L"|edge",L"Beta|firefox"});
  WriteCollections(Settings({alpha,L"Beta",L"Gamma"}, {L"Delta",extraArchived},
      {alpha+L"|edge",L"Beta|firefox",L"Gamma|chrome"}));
  std::wstring error;
  Check(RenameClientProfile(alpha,renamed,error), "Rename merges current pre-click changes in all three lists");
  ExpectSaved({renamed,L"Beta",L"Gamma"}, {L"Delta",extraArchived},
      {renamed+L"|edge",L"Beta|firefox",L"Gamma|chrome"});

  Setup({L"Beta"}, {L"Delta"}, {L"Beta|firefox"});
  WriteCollections(Settings({L"Beta",alpha}, {L"Delta"}, {L"Beta|firefox",alpha+L"|chrome"}));
  Check(RenameClientProfile(alpha,renamed,error), "Rename notices target memberships newly present only on disk");
  ExpectSaved({L"Beta",renamed}, {L"Delta"}, {L"Beta|firefox",renamed+L"|chrome"});

  Setup({}, {}, {});
  Check(RenameClientProfile(alpha,renamed,error), "Rename accepts three empty current lists");
  ExpectSaved({}, {}, {});

  Setup({alpha}, {L"Delta"}, {alpha+L"|edge",renamed+L"|chrome"});
  Check(RenameClientProfile(alpha,renamed,error), "Different browser keys for the new name remain independent");
  ExpectSaved({renamed}, {L"Delta"}, {renamed+L"|edge",renamed+L"|chrome"});

  Setup({alpha}, {L"Delta"}, {alpha,alpha+L"|edge",L"Beta|firefox"});
  g_selectedBrowser=BrowserKind::Edge;
  Check(RenameClientProfile(alpha,renamed,error), "Legacy unqualified and browser-qualified restore keys both follow the rename");
  // The established reload/prune normalizes the legacy alias to the selected
  // browser and deduplicates it against the equivalent qualified preference.
  ExpectSaved({renamed}, {L"Delta"}, {renamed+L"|edge",L"Beta|firefox"});
  Check(!ShouldRestoreTabsForClient(renamed,BrowserKind::Edge) &&
      !ShouldRestoreTabsForClient(L"Beta",BrowserKind::Firefox) &&
      ShouldRestoreTabsForClient(renamed,BrowserKind::Chrome),
      "Legacy normalization keeps the renamed and unrelated disabled settings without affecting another browser");
}
static void Collisions() {
  for (unsigned section=0; section<3; ++section) {
    Setup({alpha,L"Beta"}, {L"Delta"}, {alpha+L"|edge",L"Beta|firefox"});
    auto pins=g_pinnedClients, archives=g_archivedClients, restore=Preferences();
    if(section==0) pins.push_back(renamed);
    else if(section==1) archives={alpha,renamed};
    else restore.push_back(renamed+L"|edge");
    WriteCollections(Settings(pins,archives,restore));
    const auto before=Bytes(g_sConfigPath);
    const auto oldPinned=g_pinnedClients, oldArchived=g_archivedClients;
    const auto oldRestore=Preferences(); std::wstring error;
    Check(!RenameClientProfile(alpha,renamed,error) && !error.empty(),
        "Rename refuses a duplicate resulting membership in any of the three lists");
    Check(Bytes(g_sConfigPath)==before && g_pinnedClients==oldPinned &&
        g_archivedClients==oldArchived && Preferences()==oldRestore,
        "Collision refusal keeps exact current disk bytes and prior memory vectors");
    NotesIntact(false);
  }
}
static void PublicationConflict() {
  Setup({alpha,L"Beta"}, {L"Delta"}, {alpha+L"|edge",L"Beta|firefox"});
  const auto oldPinned=g_pinnedClients, oldArchived=g_archivedClients, oldRestore=Preferences();
  CollectionConflict conflict{Settings({alpha,L"Beta",L"Gamma"}, {L"Delta",extraArchived},
      {alpha+L"|edge",L"Beta|firefox",L"Gamma|chrome"})};
  config_persistence::SetIniPublicationHookForTest(ReplaceCollections,&conflict);
  std::wstring error;
  const bool first=RenameClientProfile(alpha,renamed,error);
  config_persistence::SetIniPublicationHookForTest(nullptr,nullptr);
  Check(conflict.attempted && conflict.changed && !first, "Real competing publication rejects the stale rename transaction");
  Check(Bytes(g_sConfigPath)==conflict.bytes && g_pinnedClients==oldPinned &&
      g_archivedClients==oldArchived && Preferences()==oldRestore,
      "Publication refusal keeps the newer exact settings and rolls memory back");
  NotesIntact(false); g_sessions.clear();
  Check(RenameClientProfile(alpha,renamed,error), "Immediate rename retry uses current disk memberships");
  ExpectSaved({renamed,L"Beta",L"Gamma"}, {L"Delta",extraArchived},
      {renamed+L"|edge",L"Beta|firefox",L"Gamma|chrome"});
}
static void PartialWrites() {
  for(bool archivedTarget:{false,true}) for(bool shortWrite:{false,true}) for(unsigned failAt:{1u,2u}) {
    Setup(archivedTarget ? std::vector<std::wstring>{L"Beta"} : std::vector<std::wstring>{alpha,L"Beta"},
          archivedTarget ? std::vector<std::wstring>{alpha,L"Delta"} : std::vector<std::wstring>{L"Delta"},
          {alpha+L"|edge",L"Beta|firefox"});
    const auto before=Bytes(g_sConfigPath);
    const auto oldPinned=g_pinnedClients, oldArchived=g_archivedClients;
    const auto oldRestore=Preferences(); const auto initialDirect=directCollectionWrites;
    const auto initialIndexed=indexedCollectionWrites, initialCount=collectionCountWrites;
    shortDirectWrite=shortWrite; failedDirectWriteAt=directCollectionWrites+failAt;
    std::wstring error; const bool saved=RenameClientProfile(alpha,renamed,error); failedDirectWriteAt=0;
    Check(!saved && directCollectionWrites-initialDirect==failAt,
        "Rename aborts the exact first or second short-write/disk-full stage failure");
    Check(Bytes(g_sConfigPath)==before && g_pinnedClients==oldPinned &&
        g_archivedClients==oldArchived && Preferences()==oldRestore,
        "Partial paired settings write commits neither disk nor collection memory");
    Check(indexedCollectionWrites==initialIndexed && collectionCountWrites==initialCount,
        "Failed direct rename writes never fall back to native per-row publication");
    NotesIntact(false);
  }
}
static void Run() {
  struct Scope {
    fs::path previousBase=base, previousShortcuts=g_sShortcutDesktopOverride;
    ~Scope(){base=previousBase;g_sShortcutDesktopOverride=previousShortcuts;}
  } scope;
  base=retained_fixture::CreateRoot(fs::current_path()/L"build",L"r15rnp");
  const unsigned before=scenarios;
  PreclickAndEmpty(); Collisions(); PublicationConflict(); PartialWrites();
  Check(scenarios-before==17, "Permanent rename suite exercises seventeen private scenarios");
  Check(!g_hGui&&!g_hClientEdit&&!g_hComboClient&&!g_hNotesWindow &&
      presentationRequests==0&&launchRequests==0&&forbiddenPaint==0&&closeTargets.empty(),
      "Rename preferences use no native windows, desktop input, foreground or real browser");
  std::cout<<"Rename preference delta: 17 private rename, collision, publication and partial-write scenarios passed.\n";
}
} // namespace rename_preference_delta
