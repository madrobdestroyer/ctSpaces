// Exercises the actual native production menu routines, with popup results and
// reentrant list changes supplied by FakePopup. No popup is displayed.
static unsigned pinnedScenarios=0;
static void NativePinnedMenuCases(){
  const fs::path fixtureRoot=g_sDataDir/L"pinned";
  const unsigned callsBefore=menuCalls;
  const auto seed=[&](){
    ++pinnedScenarios;++scenarios;
    g_sDataDir=fixtureRoot/std::to_wstring(pinnedScenarios);
    fs::create_directories(g_sDataDir/L"Sites");g_sConfigPath=g_sDataDir/L"config.ini";
    g_pinnedClients={L"Alpha",L"Bravo",L"Charlie",L"Delta",L"Echo",L"Foxtrot"};
    g_archivedClients.clear();g_clientsWithoutTabRestore.clear();g_activeProfiles.clear();g_sessions.clear();
    g_bUiEnabled=true;g_selectedBrowser=BrowserKind::Edge;g_iSelectedSessionTab=0;
    for(size_t i=0;i<g_pinnedClients.size();++i){
      fs::create_directory(g_sDataDir/L"Sites"/g_pinnedClients[i]);
      std::ofstream marker(g_sDataDir/L"Sites"/g_pinnedClients[i]/L"menu-sentinel.txt",std::ios::binary);
      marker<<"Owned fixture data: "<<i;
      g_sessions.push_back({g_pinnedClients[i],BrowserKind::Edge,static_cast<DWORD>(0x7ffff200+i)});
    }
    Check(SavePinnedClients(),"Pinned menu fixture persists its original client order");
    SetClientInputText(L"");chosenLabel.clear();menuMutation={};closeTargets.clear();
  };
  const auto removeAndPrune=[&](const wchar_t *name){
    const auto path=g_sDataDir/L"Sites"/name;
    // Only the exactly named sentinel and now-empty directory in this owned
    // per-case fixture are removed; no recursive path operation is needed.
    if(!fs::remove(path/L"menu-sentinel.txt")||!fs::remove(path))
      throw std::runtime_error("Cannot remove the private missing-client fixture");
    PrunePinnedClients();
  };
  const auto overflow=[&](const wchar_t *label,const std::function<void()> &mutation,const wchar_t *expected,bool selectOnly,const char *message){
    seed();chosenLabel=label;menuMutation=mutation;ShowPinnedOverflowMenu(g_hGui,selectOnly);
    std::wcout<<L"PINNED RESULT: chosen="<<label<<L" selected="<<GetClientInputText()<<L" expected="<<expected<<L'\n';
    Check(GetClientInputText()==expected&&closeTargets.empty(),message);
  };
  overflow(L"Echo",[]{},L"Echo",true,"Unchanged pinned overflow Select targets the displayed client");
  overflow(L"Echo",[&]{removeAndPrune(L"Alpha");},L"Echo",true,"Pruning an earlier missing pin cannot retarget overflow Select to the next client");
  overflow(L"Echo",[&]{removeAndPrune(L"Echo");},L"",true,"A removed pinned overflow target cannot select its neighbor");
  overflow(L"Foxtrot",[&]{removeAndPrune(L"Alpha");},L"Foxtrot",true,"A shifted last overflow target remains selectable");
  overflow(L"Echo",[]{std::reverse(g_pinnedClients.begin(),g_pinnedClients.end());},L"Echo",true,"Reordering pins during overflow selection keeps the displayed client identity");
  overflow(L"Echo",[&]{removeAndPrune(L"Alpha");},L"Echo",false,"Pruning an earlier missing pin cannot retarget overflow Open to another session");
  overflow(L"",[&]{removeAndPrune(L"Alpha");},L"",true,"Canceling pinned overflow after pruning does not select a client");
  const auto context=[&](const wchar_t *label,const std::function<void()> &mutation,const wchar_t *expected,const char *message){
    seed();chosenLabel=label;menuMutation=mutation;
    ShowPinnedClientOptionsMenu(g_hGui,g_pinnedClients[1],{});
    std::wcout<<L"PINNED CONTEXT RESULT: selected="<<GetClientInputText()<<L" expected="<<expected<<L'\n';
    Check(GetClientInputText()==expected&&closeTargets.empty(),message);
  };
  context(L"Select client",[]{},L"Bravo","Unchanged pinned context Select targets the selected pin");
  context(L"Select client",[&]{removeAndPrune(L"Alpha");},L"Bravo","Pruning an earlier pin cannot change a context-menu reference to another client");
  context(L"Open client",[&]{removeAndPrune(L"Alpha");},L"Bravo","Pinned context Open remains bound to the original client after pruning");
  struct ContextNamespaceObservation {
    bool attempted=false,moved=false,original=false,sentinel=false,neighbor=false,displaced=false;
    DWORD error=ERROR_SUCCESS;
  } namespaceResult;
  context(L"Select client",[&]{
    const auto original=g_sDataDir/L"Sites"/L"Bravo";
    const auto displaced=g_sDataDir/L"Sites"/L"Bravo-context-displaced";
    const auto directoryIdentity=[](const fs::path &path,BY_HANDLE_FILE_INFORMATION &info){
      HANDLE handle=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,
          FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
          FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
      if(handle==INVALID_HANDLE_VALUE)return false;
      const bool safe=GetFileInformationByHandle(handle,&info)&&
          (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&
          !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT);
      CloseHandle(handle);return safe;
    };
    const auto readSentinel=[](const fs::path &path){
      std::ifstream input(path,std::ios::binary);
      return std::string(std::istreambuf_iterator<char>(input),{});
    };
    BY_HANDLE_FILE_INFORMATION before{},after{};
    const bool captured=directoryIdentity(original,before);
    // Do not remove payload first: the live context menu owns this original
    // namespace now. A blocked whole-directory move must preserve its data.
    namespaceResult.attempted=true;
    namespaceResult.moved=MoveFileExW(original.c_str(),displaced.c_str(),0)!=FALSE;
    namespaceResult.error=namespaceResult.moved?ERROR_SUCCESS:GetLastError();
    namespaceResult.original=captured&&directoryIdentity(original,after)&&
        before.dwVolumeSerialNumber==after.dwVolumeSerialNumber&&
        before.nFileIndexHigh==after.nFileIndexHigh&&before.nFileIndexLow==after.nFileIndexLow;
    namespaceResult.sentinel=readSentinel(original/L"menu-sentinel.txt")=="Owned fixture data: 1";
    namespaceResult.neighbor=readSentinel(g_sDataDir/L"Sites"/L"Charlie"/L"menu-sentinel.txt")=="Owned fixture data: 2";
    namespaceResult.displaced=GetFileAttributesW(displaced.c_str())!=INVALID_FILE_ATTRIBUTES;
    Check(!namespaceResult.moved&&namespaceResult.error==ERROR_SHARING_VIOLATION,
          "The actual context-menu namespace lease blocks removal of its original client with native sharing error 32");
    Check(namespaceResult.original&&namespaceResult.sentinel&&namespaceResult.neighbor&&!namespaceResult.displaced,
          "Blocked context removal preserves the exact original directory identity, sentinel bytes and neighboring client");
    // If the move unexpectedly succeeds, retain that evidence and fail. Never
    // roll a different object back into the original client pathname.
    PrunePinnedClients();
  },L"Bravo","A blocked pinned context removal keeps Select on the original Bravo, never its neighbor");
  std::wcout<<L"Pinned context namespace: attempted="<<namespaceResult.attempted
      <<L"; moved="<<namespaceResult.moved<<L"; error="<<namespaceResult.error
      <<L"; original="<<namespaceResult.original<<L"; sentinel="<<namespaceResult.sentinel
      <<L"; neighbor="<<namespaceResult.neighbor<<L"; displaced="<<namespaceResult.displaced
      <<L"; selected="<<GetClientInputText()<<L'\n';
  context(L"",[&]{removeAndPrune(L"Alpha");},L"","Canceling a pinned context menu after pruning does not select a client");
  Check(menuCalls-callsBefore==pinnedScenarios,"All pinned cases traverse the actual production popup routines");
  Check(suppressedClipboardRequests==5,"Pinned context cases suppress every clipboard-open request without reading clipboard data");
  std::cout<<"Pinned menu: "<<pinnedScenarios<<" reentrant/control scenarios; native popups and clipboard reads suppressed.\n";
  menuMutation={};chosenLabel.clear();g_sessions.clear();g_pinnedClients.clear();
}
