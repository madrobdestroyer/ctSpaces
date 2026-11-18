static void SendLauncherPointer(UINT message, POINT point) {
  const auto buttons = message == WM_LBUTTONDOWN || message == WM_MOUSEMOVE ? MK_LBUTTON : 0;
  WndProc(g_hGui, message, buttons,
          MAKELPARAM(point.x, point.y));
}

static DWORD SelectedFixtureSession() {
  return g_iSelectedSessionTab > 0 ? g_sessions.at(g_iSelectedSessionTab - 1).pid : 0;
}

static void NativeBusyPointerGestures(bool before) {
  const auto alpha = g_sDataDir / L"Sites" / L"Alpha";
  fs::create_directories(alpha);
  WriteBrowserSlotFixture(alpha / kClientSchemaMarkerName, kClientSchemaMarkerText);
  const auto prepare = [] {
    Seed();
    g_selectedBrowser = BrowserKind::Edge;
    g_pinnedClients = {L"Alpha"};
    g_rcPinnedArea = {12, 42, 520, 82};
    g_rcSessionTabs = {0, 0, 600, 32};
    BuildPinnedClientRects();
    BuildSessionTabRects(g_hGui, nullptr);
    SetUiState(true);
    foregroundRequest = nullptr;
  };
  const auto interruptedClick = [](POINT point) {
    SendLauncherPointer(WM_LBUTTONDOWN, point);
    Check(fixtureCapture == g_hGui, "The private launcher owns the pending mouse press");
    g_bClientNotesBusy = true;
    SetUiState(false);
    Check(!g_bUiEnabled, "The modal operation disables the launcher");
    g_bClientNotesBusy = false;
    SetUiState(true);
    SendLauncherPointer(WM_LBUTTONUP, point);
  };
  const auto click = [](POINT point) {
    SendLauncherPointer(WM_LBUTTONDOWN, point);
    SendLauncherPointer(WM_LBUTTONUP, point);
  };

  prepare();
  const auto pinPoint = LauncherFixtureCenter(g_pinnedClientRects.at(0));
  interruptedClick(pinPoint);
  const bool pinOpened = SelectedFixtureSession() == alphaPid &&
                         foregroundRequest == fixtureBrowserWindows[0];
  Check(before ? pinOpened : !SelectedFixtureSession() && !foregroundRequest,
        "A pin press started before a modal operation cannot open its browser later");
  foregroundRequest = nullptr;
  click(pinPoint);
  Check(SelectedFixtureSession() == alphaPid && foregroundRequest == fixtureBrowserWindows[0],
        "A fresh pin click works after the launcher is enabled");

  prepare();
  const auto tab = g_sessionTabRects.at(2);
  const POINT tabPoint{tab.left + 4, (tab.top + tab.bottom) / 2};
  Check(HitTestSessionTab(g_hGui, tabPoint) == 2 && HitTestSessionClose(g_hGui, tabPoint) < 0,
        "The test targets the visible session body");
  interruptedClick(tabPoint);
  const bool sessionShown = SelectedFixtureSession() == bravoPid &&
                            foregroundRequest == fixtureBrowserWindows[1];
  Check(before ? sessionShown : !SelectedFixtureSession() && !foregroundRequest,
        "A tab press started before a modal operation cannot show its browser later");
  foregroundRequest = nullptr;
  click(tabPoint);
  Check(SelectedFixtureSession() == bravoPid && foregroundRequest == fixtureBrowserWindows[1],
        "A fresh tab click works after the launcher is enabled");

  prepare();
  const auto closePoint = LauncherFixtureCenter(g_sessionTabCloseRects.at(2));
  interruptedClick(closePoint);
  const bool closeRequested = closeTargets == std::vector<DWORD>{bravoPid};
  Check(before ? closeRequested : closeTargets.empty(),
        "A Close press started before a modal operation cannot close its browser later");
  closeTargets.clear();
  click(closePoint);
  Check(closeTargets == std::vector<DWORD>{bravoPid},
        "A fresh Close click works after the launcher is enabled");
  Check(!fixtureCapture && !launchRequests && !forbiddenPaint && terminationTargets.empty(),
        "The three gestures use private modeled browsers and release capture");
  std::cout << "LAUNCHER_BUSY_GESTURE pin=" << pinOpened
            << " session=" << sessionShown << " close=" << closeRequested << '\n';
}

static void NativeLauncherWorkflowRound(unsigned round) {
  const auto clients = g_sDataDir / L"Sites";
  const auto seedClient = [&](const std::wstring &name) {
    const auto root = clients / name;
    fs::create_directories(root);
    WriteBrowserSlotFixture(root / kClientSchemaMarkerName, kClientSchemaMarkerText);
    return root;
  };
  const auto alpha = seedClient(L"Alpha");
  const auto bravo = seedClient(L"Bravo");
  const auto charlie = seedClient(L"Charlie");
  Seed();
  g_selectedBrowser = BrowserKind::Edge;
  g_pinnedClients = {L"Alpha", L"Bravo", L"Charlie"};
  g_rcPinnedArea = {12, 42, 520, 82};
  g_rcSessionTabs = {0, 0, 600, 32};
  BuildPinnedClientRects();
  BuildSessionTabRects(g_hGui, nullptr);
  SetUiState(true);

  const auto command = [](UINT id, UINT notification = 0) {
    WndProc(g_hGui, WM_COMMAND, MAKEWPARAM(id, notification), 0);
  };
  const auto click = [](POINT point) {
    SendLauncherPointer(WM_LBUTTONDOWN, point);
    SendLauncherPointer(WM_LBUTTONUP, point);
  };
  const auto readLayout = [](const fs::path &client) {
    client_tab_layout::Override choice{};
    if (!client_tab_layout::Read(client, choice))
      throw std::runtime_error("Cannot read the private tab-layout choice");
    return choice;
  };

  const char *name = nullptr;
  switch (round) {
  case 1: {
    name = "busy-transition-finishes-pin-reorder";
    if (!SavePinnedClients())
      throw std::runtime_error("Cannot seed the private pin order");
    const auto first = LauncherFixtureCenter(g_pinnedClientRects.at(0));
    const auto last = LauncherFixtureCenter(g_pinnedClientRects.at(2));
    SendLauncherPointer(WM_LBUTTONDOWN, first);
    SendLauncherPointer(WM_MOUSEMOVE, last);
    const std::vector<std::wstring> reordered{L"Bravo", L"Charlie", L"Alpha"};
    Check(g_pinnedClients == reordered && g_bMainDragActive,
          "Dragging the pin moves it past both neighbors before the modal operation");
    g_bClientNotesBusy = true;
    SetUiState(false);
    Check(!fixtureCapture && g_mainDragKind == MainDragKind::None,
          "The modal transition ends the active drag and releases capture");
    g_bClientNotesBusy = false;
    SetUiState(true);
    SendLauncherPointer(WM_LBUTTONUP, last);
    LoadPinnedClients();
    Check(g_pinnedClients == reordered && !SelectedFixtureSession() && !foregroundRequest,
          "Capture cancellation preserves the completed reorder without opening a browser");
    break;
  }
  case 2: {
    name = "unicode-pin-and-case-insensitive-unpin";
    const std::wstring official = L"\u00c9quipe Support";
    seedClient(official);
    g_allClientNames = {L"Alpha", L"Bravo", L"Charlie", official};
    if (!SavePinnedClients())
      throw std::runtime_error("Cannot seed the private pin list");
    SetClientInputText(L"\u00e9QUIPE support");
    command(IDC_BTN_PIN, BN_CLICKED);
    LoadPinnedClients();
    Check(g_pinnedClients.size() == 4 && g_pinnedClients.back() == official,
          "Pinning a differently cased client stores its existing Unicode folder name");
    SetClientInputText(L"\u00e9quipe SUPPORT");
    command(IDC_BTN_PIN, BN_CLICKED);
    LoadPinnedClients();
    Check(g_pinnedClients == std::vector<std::wstring>{L"Alpha", L"Bravo", L"Charlie"} &&
              fs::is_directory(clients / official),
          "Unpinning with a different spelling case preserves the client and other pins");
    break;
  }
  case 3:
    name = "selected-firefox-restore-versus-global-edge";
    CreateBrowserSlotFixture(charlie, BrowserKind::Firefox);
    CreateBrowserSlotFixture(charlie, BrowserKind::Edge);
    SelectSessionTab(3, false);
    Check(g_selectedBrowser == BrowserKind::Edge && g_bRestoreTabsToggleAvailable &&
              g_bRestoreTabsForSelection,
          "The open Firefox tab offers Restore tabs while the launcher default is Edge");
    command(IDC_BTN_RESTORE_TABS, BN_CLICKED);
    LoadRestoreTabsPreferences();
    Check(!ShouldRestoreTabsForClient(L"Charlie", BrowserKind::Firefox) &&
              ShouldRestoreTabsForClient(L"Charlie", BrowserKind::Edge) &&
              SelectedFixtureSession() == charliePid,
          "The visible Restore tabs action changes only the selected Firefox slot");
    break;
  case 4:
    name = "client-tab-layout-override-and-follow-global";
    SetClientInputText(L"Alpha");
    command(IDM_TAB_LAYOUT_GLOBAL_VERTICAL);
    command(IDM_TAB_LAYOUT_CLIENT_HORIZONTAL);
    LoadGlobalTabLayoutPreference();
    Check(g_globalTabLayout == client_tab_layout::Layout::Vertical &&
              readLayout(alpha) == client_tab_layout::Override::Horizontal &&
              readLayout(bravo) == client_tab_layout::Override::FollowGlobal,
          "A client-specific horizontal choice is independent of its peer and the global vertical choice");
    command(IDM_TAB_LAYOUT_CLIENT_FOLLOW);
    Check(client_tab_layout::Resolve(g_globalTabLayout, readLayout(alpha)) ==
              client_tab_layout::Layout::Vertical,
          "Clearing the client override restores the saved global choice");
    break;
  case 5: {
    name = "locked-client-layout-preserves-neighbor";
    if (!client_tab_layout::Write(alpha, client_tab_layout::Override::Vertical) ||
        !client_tab_layout::Write(bravo, client_tab_layout::Override::Horizontal))
      throw std::runtime_error("Cannot seed private tab-layout records");
    const auto record = alpha / client_tab_layout::kFileName;
    HANDLE file = CreateFileW(record.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
      throw std::runtime_error("Cannot open the private tab-layout record for the lock fixture");
    std::unique_ptr<void, decltype(&CloseHandle)> lock(file, CloseHandle);
    SetClientInputText(L"Alpha");
    command(IDM_TAB_LAYOUT_CLIENT_HORIZONTAL);
    Check(readLayout(alpha) == client_tab_layout::Override::Vertical &&
              readLayout(bravo) == client_tab_layout::Override::Horizontal,
          "A refused tab-layout write preserves the selected client's prior choice and its peer");
    lock.reset();
    command(IDM_TAB_LAYOUT_CLIENT_HORIZONTAL);
    Check(readLayout(alpha) == client_tab_layout::Override::Horizontal &&
              readLayout(bravo) == client_tab_layout::Override::Horizontal,
          "The same preference saves after the private file lock is released");
    break;
  }
  case 6: {
    name = "client-list-preview-and-commit-from-session";
    SelectSessionTab(3, false);
    g_allClientNames = {L"Alpha", L"Bravo", L"Charlie"};
    FilterClientList(L"ra", false);
    Check(SendMessageW(g_hComboClient, CB_GETCOUNT, 0, 0) == 1 &&
              SendMessageW(g_hComboClient, CB_SETCURSEL, 0, 0) == 0,
          "The hidden client list has one matching row to preview");
    command(102, CBN_SELCHANGE);
    Check(SelectedFixtureSession() == charliePid && GetClientInputText() == L"Charlie",
          "Previewing a list row keeps the selected browser and client field");
    command(102, CBN_SELENDOK);
    Check(!SelectedFixtureSession() && GetClientInputText() == L"Bravo" && !foregroundRequest,
          "Committing the row switches to Open mode for the chosen client");
    break;
  }
  case 7: {
    name = "windowless-close-retry-retains-session";
    SelectSessionTab(2, false);
    const auto close = LauncherFixtureCenter(g_sessionTabCloseRects.at(2));
    fixtureBrowserWindowsReady = false;
    click(close);
    Check(closeTargets.empty() && terminationTargets.empty() &&
              SelectedFixtureSession() == bravoPid && g_sessions.size() == 3,
          "A browser without a visible window remains selected when normal Close cannot be posted");
    fixtureBrowserWindowsReady = true;
    click(close);
    Check(closeTargets == std::vector<DWORD>{bravoPid} && g_sessions.size() == 3,
          "A fresh click retries normal Close once the modeled window is available");
    break;
  }
  case 8:
    name = "closed-firefox-returns-to-open-edge-slot";
    g_sessions[0].clientName = L"Charlie";
    CreateBrowserSlotFixture(charlie, BrowserKind::Edge);
    CreateBrowserSlotFixture(charlie, BrowserKind::Firefox);
    SelectSessionTab(3, false);
    OnSessionEnded(charliePid);
    Check(!SelectedFixtureSession() && GetClientInputText() == L"Charlie" &&
              g_sessions.size() == 2 && !foregroundRequest,
          "Firefox exit returns to Open while preserving the shared client and its Edge tab");
    command(IDM_BROWSER_EDGE);
    Check(SelectedFixtureSession() == alphaPid && g_bRestoreTabsToggleAvailable &&
              GetClientInputText() == L"Charlie" && !foregroundRequest,
          "Choosing Edge from Open selects its existing slot without raising a window");
    break;
  case 9: {
    name = "cancel-mode-clears-session-close-press";
    SelectSessionTab(3, false);
    const auto close = LauncherFixtureCenter(g_sessionTabCloseRects.at(2));
    SendLauncherPointer(WM_LBUTTONDOWN, close);
    WndProc(g_hGui, WM_CANCELMODE, 0, 0);
    Check(!fixtureCapture, "Cancel mode releases the held session close capture");
    SendLauncherPointer(WM_LBUTTONUP, close);
    Check(closeTargets.empty() && SelectedFixtureSession() == charliePid,
          "A release after cancel mode cannot close the unrelated browser");
    click(close);
    Check(closeTargets == std::vector<DWORD>{bravoPid},
          "The same Close control accepts a fresh completed click");
    break;
  }
  case 10: {
    name = "pin-capacity-refusal-and-slot-reuse";
    for (const auto *additional : {L"Delta", L"Echo", L"Foxtrot", L"Golf", L"Hotel", L"India"})
      seedClient(additional);
    g_pinnedClients = {L"Alpha", L"Bravo", L"Charlie", L"Delta", L"Echo", L"Foxtrot", L"Golf", L"Hotel"};
    if (!SavePinnedClients())
      throw std::runtime_error("Cannot seed the full private pin list");
    const auto before = ReadBrowserSlotSettings();
    SetClientInputText(L"India");
    command(IDC_BTN_PIN, BN_CLICKED);
    Check(g_pinnedClients.size() == MAX_PINNED_CLIENTS && !IsClientPinned(L"India") &&
              ReadBrowserSlotSettings() == before,
          "Attempting a ninth pin preserves the full saved list");
    SetClientInputText(L"Foxtrot");
    command(IDC_BTN_PIN, BN_CLICKED);
    SetClientInputText(L"India");
    command(IDC_BTN_PIN, BN_CLICKED);
    LoadPinnedClients();
    Check(g_pinnedClients == std::vector<std::wstring>{
              L"Alpha", L"Bravo", L"Charlie", L"Delta", L"Echo", L"Golf", L"Hotel", L"India"},
          "Unpinning a client allows a new pin in the freed slot and preserves order");
    break;
  }
  default:
    throw std::runtime_error("Unexpected launcher workflow round");
  }

  Check(!fixtureCapture && g_mainDragKind == MainDragKind::None &&
            !launchRequests && !forbiddenPaint && !suppressedClipboardRequests &&
            terminationTargets.empty() && (round == 7 || round == 9 || closeTargets.empty()),
        "The launcher workflow stays inside private windows, client data and modeled browser lifetimes");
  std::cout << "APP_LAUNCHER_ROUND round=" << round << " case=" << name << '\n';
}
