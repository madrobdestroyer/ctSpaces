// Actual production popup construction and routing with a process-local URL.
// FakePopup supplies the result; no visible native menu or clipboard is used.
static void NativeBusyLinkCases(){
  const auto oldData=g_sDataDir,oldConfig=g_sConfigPath,oldEdge=g_sEdgePath;
  const auto oldBrowser=g_selectedBrowser;
  g_sDataDir=retained_fixture::CreateRoot(fs::current_path()/L"build"/L"qa-busy",L"fixture");
  g_sConfigPath=g_sDataDir/L"config.ini";
  const auto profile=g_sDataDir/L"Sites"/L"Alpha"/L"Edge",fakeExe=g_sDataDir/L"msedge.exe";
  fs::create_directories(profile);
  const auto write=[](const fs::path &path,const char *text){std::ofstream f(path,std::ios::binary);f<<text;f.close();if(!f)throw std::runtime_error("Cannot write private busy-link sentinel");};
  const auto bytes=[](const fs::path &path){std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Cannot read private busy-link sentinel");return std::string(std::istreambuf_iterator<char>(f),{});};
  write(g_sConfigPath,"[Settings]\r\nPinnedClients=Alpha\r\n");
  write(profile/L"owned.txt","PRIVATE PROFILE UNCHANGED");
  write(fakeExe,"NONEXECUTABLE OWNED BROWSER SENTINEL");
  const auto configBefore=bytes(g_sConfigPath),profileBefore=bytes(profile/L"owned.txt"),exeBefore=bytes(fakeExe);
  const std::wstring url=L"https://example.invalid/launcher-busy-review";
  ownedClipboard=GlobalAlloc(GMEM_MOVEABLE,(url.size()+1)*sizeof(wchar_t));
  if(!ownedClipboard)throw std::runtime_error("Cannot allocate private clipboard buffer");
  void *buffer=GlobalLock(ownedClipboard);
  if(!buffer)throw std::runtime_error("Cannot lock private clipboard buffer");
  memcpy(buffer,url.c_str(),(url.size()+1)*sizeof(wchar_t));GlobalUnlock(ownedClipboard);
  const auto callsBefore=menuCalls,clipboardBefore=suppressedClipboardRequests,dataBefore=clipboardDataRequests,closeBefore=clipboardCloseRequests;
  const auto createsBefore=createAttempts,launchesBefore=launchRequests;
  g_selectedBrowser=BrowserKind::Edge;g_sEdgePath=fakeExe;
  const auto seed=[&](bool active){
    g_isLaunchInFlight=false;g_bUiEnabled=true;g_activeProfiles.clear();g_sessions.clear();g_iSelectedSessionTab=0;
    SetClientInputText(L"");
    if(active){
      auto token=std::shared_ptr<void>(reinterpret_cast<void *>(static_cast<uintptr_t>(alphaPid)),[](void *){});
      g_activeProfiles.emplace(ClientBrowserKey{L"Alpha",BrowserKind::Edge},ActiveProfileInfo{alphaPid,fakeExe,profile,ProfileType::Standard,std::move(token)});
      g_sessions={{L"Alpha",BrowserKind::Edge,alphaPid}};
    }
    chosenLabel=L"Open copied link";menuMutation={};attemptedExecutable.clear();attemptedCommand.clear();
  };
  const auto run=[&](){ShowPinnedClientOptionsMenu(g_hGui,L"Alpha",{});++scenarios;};
  seed(true);auto previous=createAttempts;run();
  Check(createAttempts-previous==1,"Enabled active Chromium URL control reaches exactly one intercepted launch");
  Check(attemptedExecutable==fakeExe.wstring()&&attemptedCommand.find(url)!=std::wstring::npos&&attemptedCommand.find(profile.wstring())!=std::wstring::npos,"URL control retains the exact owned executable, client profile and captured address");
  seed(true);previous=createAttempts;menuMutation=[]{SetUiState(false);g_isLaunchInFlight=true;};run();
  Check(!g_bUiEnabled&&g_isLaunchInFlight.load()&&!IsWindowEnabled(g_hBtnGo),"Real SetUiState disables launcher controls during the popup transition");
  Check(createAttempts==previous,"Active Chromium copied-link command is refused when popup returns during a launch");
  seed(false);previous=createAttempts;menuMutation=[]{SetUiState(false);g_isLaunchInFlight=true;};run();
  Check(createAttempts==previous&&GetClientInputText().empty(),"Inactive copied-link command is refused during the same transition without changing selection");
  seed(true);previous=createAttempts;chosenLabel.clear();menuMutation=[]{SetUiState(false);g_isLaunchInFlight=true;};run();
  Check(createAttempts==previous,"Canceled copied-link popup cannot launch a browser during the transition");
  seed(true);previous=createAttempts;menuMutation=[]{SetUiState(false);};run();
  Check(!g_bUiEnabled&&!g_isLaunchInFlight.load()&&createAttempts==previous,"Disabled-only launcher state independently refuses active Chromium copied-link command");
  seed(true);previous=createAttempts;menuMutation=[]{g_isLaunchInFlight=true;};run();
  Check(g_bUiEnabled&&g_isLaunchInFlight.load()&&createAttempts==previous,"In-flight-only launch state independently refuses active Chromium copied-link command");
  Check(menuCalls-callsBefore==6&&suppressedClipboardRequests-clipboardBefore==6&&clipboardDataRequests-dataBefore==6&&clipboardCloseRequests-closeBefore==6&&!clipboardOpen,"All six URL cases use actual menu routing and balanced private clipboard adapters");
  Check(bytes(g_sConfigPath)==configBefore&&bytes(profile/L"owned.txt")==profileBefore&&bytes(fakeExe)==exeBefore,"Busy URL routing preserves exact config, isolated profile and sentinel executable bytes");
  Check(createAttempts-createsBefore==1&&launchRequests-launchesBefore==1&&forbiddenPaint==0&&closeTargets.empty(),"Only enabled URL control requests one intercepted process; busy cases request no shell, tray or process actions");
  std::cout<<"Launcher busy link: 6 scenarios; intercepted control attempts="<<createAttempts-createsBefore<<"; real OS/browser/clipboard calls=0.\n";
  g_isLaunchInFlight=false;g_bUiEnabled=true;g_activeProfiles.clear();g_sessions.clear();menuMutation={};chosenLabel.clear();
  GlobalFree(ownedClipboard);ownedClipboard=nullptr;
  g_sDataDir=oldData;g_sConfigPath=oldConfig;g_sEdgePath=oldEdge;g_selectedBrowser=oldBrowser;
}
