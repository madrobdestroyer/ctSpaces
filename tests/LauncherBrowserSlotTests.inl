// Production launcher commands exercised with hidden controls and private profiles.
static void WriteBrowserSlotFixture(const fs::path &path, std::string_view value) {
  std::ofstream output(path, std::ios::binary);
  output.write(value.data(), static_cast<std::streamsize>(value.size()));
  output.close();
  if (!output)
    throw std::runtime_error("Cannot write the private browser profile marker");
}

static void CreateBrowserSlotFixture(const fs::path &client, BrowserKind browser) {
  const auto slot = client / L"Browsers" / GetBrowserId(browser);
  const auto profile = slot / L"Profile";
  fs::create_directories(profile);
  WriteBrowserSlotFixture(slot / kBrowserSchemaMarkerName, GetBrowserMarkerText(browser));
  if (IsChromiumBrowser(browser)) {
    fs::create_directory(profile / L"Default");
    WriteBrowserSlotFixture(profile / L"ctSpaces", "ctSpaces-profile=2\r\n");
  }
}

static std::string ReadBrowserSlotSettings() {
  std::ifstream input(g_sConfigPath, std::ios::binary);
  if (!input)
    throw std::runtime_error("Cannot read the private browser settings");
  return {std::istreambuf_iterator<char>(input), {}};
}

static void NativeRestoreSlotCases(bool before) {
  const auto clients = g_sDataDir / L"Sites";
  const auto alpha = clients / L"Alpha";
  const auto legacy = clients / L"Legacy";
  const auto notebook = clients / L"Notes only";
  fs::create_directories(alpha);
  fs::create_directories(legacy / L"Default");
  fs::create_directories(notebook);
  WriteBrowserSlotFixture(alpha / kClientSchemaMarkerName, kClientSchemaMarkerText);
  WriteBrowserSlotFixture(notebook / kClientSchemaMarkerName, kClientSchemaMarkerText);
  CreateBrowserSlotFixture(alpha, BrowserKind::Edge);
  g_sessions.clear();
  g_pinnedClients = {L"Alpha"};
  g_archivedClients.clear();
  g_iSelectedSessionTab = 0;
  g_bUiEnabled = true;
  g_clientsWithoutTabRestore = {
      MakeClientBrowserPreferenceKey(L"Alpha", BrowserKind::Firefox)};
  if (!SaveRestoreTabsPreferences())
    throw std::runtime_error("Cannot seed the private saved browser preference");

  const auto select = [](const wchar_t *client, BrowserKind browser) {
    ++scenarios;
    SetClientInputText(client);
    g_selectedBrowser = browser;
    UpdateRestoreTabsToggleState();
  };
  const auto toggleAvailable = [](bool expected) {
    return g_bRestoreTabsToggleAvailable == expected &&
           (IsWindowEnabled(g_hBtnRestoreTabs) != FALSE) == expected;
  };
  select(L"Alpha", BrowserKind::Edge);
  Check(toggleAvailable(true) && g_bRestoreTabsForSelection,
        "An existing Edge slot offers its default Restore tabs setting");

  select(L"Alpha", BrowserKind::Chrome);
  const bool missingEnabled = toggleAvailable(true) && g_bRestoreTabsForSelection;
  Check(before ? missingEnabled : toggleAvailable(false) && !g_bRestoreTabsForSelection,
        "A missing Chrome slot reflects its first launch's fresh startup");
  const auto chromeSettings = ReadBrowserSlotSettings();
  WndProc(g_hGui, WM_COMMAND, MAKEWPARAM(IDC_BTN_RESTORE_TABS, BN_CLICKED), 0);
  const bool missingWritten = ReadBrowserSlotSettings() != chromeSettings;
  Check(missingWritten == before && !fs::exists(alpha / L"Browsers" / L"chrome"),
        "A missing slot's disabled command leaves settings and profile folders alone");

  select(L"Alpha", BrowserKind::Firefox);
  Check(toggleAvailable(before) && !g_bRestoreTabsForSelection,
        "A saved preference does not make a missing Firefox slot available");
  bool sawRestoreItem = false;
  bool restoreItemEnabled = false;
  bool restoreItemChecked = false;
  popupInspect = [&](HMENU menu) {
    const UINT state = GetMenuState(menu, 47102, MF_BYCOMMAND);
    sawRestoreItem = state != static_cast<UINT>(-1);
    restoreItemEnabled = sawRestoreItem && (state & (MF_DISABLED | MF_GRAYED)) == 0;
    restoreItemChecked = sawRestoreItem && (state & MF_CHECKED) != 0;
  };
  chosenLabel = L"Restore tabs";
  const auto firefoxSettings = ReadBrowserSlotSettings();
  ShowPinnedClientOptionsMenu(g_hGui, L"Alpha", {});
  popupInspect = {};
  chosenLabel.clear();
  Check(sawRestoreItem && restoreItemEnabled == before && !restoreItemChecked,
        "The pinned Restore tabs command is unchecked and disabled until that slot exists");
  const bool pinnedWritten = ReadBrowserSlotSettings() != firefoxSettings;
  Check(pinnedWritten == before && !fs::exists(alpha / L"Browsers" / L"firefox"),
        "A stale disabled pinned command cannot remove a dormant preference or create a profile");
  // Retain the same initial setting for the subsequent first-slot control.
  if (before && !SaveRestoreTabsPreference(
                    MakeClientBrowserPreferenceKey(L"Alpha", BrowserKind::Firefox), true))
    throw std::runtime_error("Cannot restore the baseline fixture preference");

  CreateBrowserSlotFixture(alpha, BrowserKind::Firefox);
  select(L"Alpha", BrowserKind::Firefox);
  Check(toggleAvailable(true) && !g_bRestoreTabsForSelection,
        "The dormant preference applies when the Firefox slot becomes available");
  WndProc(g_hGui, WM_COMMAND, MAKEWPARAM(IDC_BTN_RESTORE_TABS, BN_CLICKED), 0);
  Check(toggleAvailable(true) && g_bRestoreTabsForSelection &&
            ShouldRestoreTabsForClient(L"Alpha", BrowserKind::Edge),
        "The existing Firefox slot changes without altering Edge");

  select(L"Notes only", BrowserKind::Edge);
  Check(before ? toggleAvailable(true) : toggleAvailable(false) && !g_bRestoreTabsForSelection,
        "A Notes-only client has no browser session to restore");

  select(L"Legacy", BrowserKind::Chrome);
  Check(toggleAvailable(true) && g_bRestoreTabsForSelection &&
            !fs::exists(legacy / kLegacyBrowserBindingMarkerName),
        "An unbound Chromium client offers its existing legacy session without binding it early");
  select(L"Legacy", BrowserKind::Firefox);
  Check(before ? toggleAvailable(true) : toggleAvailable(false) && !g_bRestoreTabsForSelection,
        "An unbound legacy client still needs a first Firefox slot");
  WriteBrowserSlotFixture(legacy / kLegacyBrowserBindingMarkerName,
                          GetLegacyBrowserBindingText(BrowserKind::Chrome));
  select(L"Legacy", BrowserKind::Chrome);
  Check(toggleAvailable(true) && g_bRestoreTabsForSelection,
        "The browser bound to legacy storage can still restore its existing session");
  select(L"Legacy", BrowserKind::Edge);
  Check(before ? toggleAvailable(true) : toggleAvailable(false) && !g_bRestoreTabsForSelection,
        "Another Chromium browser cannot restore the Chrome-bound legacy session");
  Check(!fs::exists(legacy / L"Browsers") && !launchRequests && !forbiddenPaint,
        "Inspecting Restore tabs creates no profile and requests no external action");
  std::cout << "RESTORE_SLOT missingEnabled=" << missingEnabled
            << " missingWritten=" << missingWritten
            << " pinnedWritten=" << pinnedWritten << '\n';
}

static void NativeBrowserSwitchCases(bool before) {
  const auto clients = g_sDataDir / L"Sites";
  const auto alpha = clients / L"Alpha";
  fs::create_directories(alpha);
  WriteBrowserSlotFixture(alpha / kClientSchemaMarkerName, kClientSchemaMarkerText);
  CreateBrowserSlotFixture(alpha, BrowserKind::Edge);
  CreateBrowserSlotFixture(alpha, BrowserKind::Chrome);
  g_archivedClients.clear();
  g_pinnedClients.clear();
  g_bUiEnabled = true;
  const auto title = [](HWND window) {
    wchar_t text[64]{};
    GetWindowTextW(window, text, static_cast<int>(std::size(text)));
    return std::wstring(text);
  };
  const auto seed = [](bool chromeOpen) {
    ++scenarios;
    g_selectedBrowser = BrowserKind::Edge;
    if (!SaveBrowserPreference())
      throw std::runtime_error("Cannot seed private Edge preference");
    g_sessions = {{L"Alpha", BrowserKind::Edge, alphaPid},
                  {L"Other", BrowserKind::Chrome, bravoPid}};
    if (chromeOpen)
      g_sessions.push_back({L"Alpha", BrowserKind::Chrome, charliePid});
    SelectSessionTab(1, false);
  };
  const auto choose = [](UINT command) {
    WndProc(g_hGui, WM_COMMAND, MAKEWPARAM(command, 0), 0);
  };
  const auto browserOnDisk = [] {
    wchar_t value[16]{};
    GetPrivateProfileStringW(L"user", L"browser", L"", value,
                             static_cast<DWORD>(std::size(value)), g_sConfigPath.c_str());
    return std::wstring(value);
  };

  seed(true);
  const auto presentationBefore = presentationRequests;
  choose(IDM_BROWSER_CHROME);
  const bool oldOpenTab = g_iSelectedSessionTab == 1;
  Check(g_selectedBrowser == BrowserKind::Chrome && browserOnDisk() == L"chrome" &&
            (before ? oldOpenTab : g_iSelectedSessionTab == 3) &&
            title(g_hBtnGo) == L"Show" && GetClientInputText() == L"Alpha",
        "Selecting Chrome from Edge targets the open Chrome tab for the same client");
  Check(presentationRequests == presentationBefore,
        "Selecting an existing browser slot does not raise any browser window");

  seed(false);
  choose(IDM_BROWSER_CHROME);
  const bool oldClosedTab = g_iSelectedSessionTab == 1;
  Check(g_selectedBrowser == BrowserKind::Chrome && browserOnDisk() == L"chrome" &&
            (before ? oldClosedTab && title(g_hBtnGo) == L"Show"
                    : g_iSelectedSessionTab == 0 && title(g_hBtnGo) == L"Open") &&
            GetClientInputText() == L"Alpha",
        "Selecting a closed browser returns to Open while preserving the client name");

  seed(true);
  SelectSessionTab(0, false);
  choose(IDM_BROWSER_CHROME);
  Check(g_iSelectedSessionTab == (before ? 0 : 3) && GetClientInputText() == L"Alpha",
        "The browser selector can find the same client's open slot from New");

  seed(true);
  choose(IDM_BROWSER_EDGE);
  Check(g_iSelectedSessionTab == 1 && g_selectedBrowser == BrowserKind::Edge,
        "Rechoosing the current browser retains the selected session");

  seed(true);
  const auto saved = ReadBrowserSlotSettings();
  HANDLE lock = CreateFileW(g_sConfigPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, 0, nullptr);
  if (lock == INVALID_HANDLE_VALUE)
    throw std::runtime_error("Cannot lock the private browser settings");
  choose(IDM_BROWSER_CHROME);
  CloseHandle(lock);
  Check(g_iSelectedSessionTab == 1 && g_selectedBrowser == BrowserKind::Edge &&
            ReadBrowserSlotSettings() == saved,
        "A failed browser preference save keeps the previous selection and settings");

  seed(false);
  g_sessions.push_back({L"Temp", BrowserKind::Firefox, charliePid});
  SelectSessionTab(3, false);
  choose(IDM_BROWSER_CHROME);
  Check(g_iSelectedSessionTab == 3 && g_selectedBrowser == BrowserKind::Chrome &&
            GetClientInputText() == L"Temp" && title(g_hBtnGo) == L"Show",
        "Changing the default browser keeps an open temporary session selected");
  Check(!launchRequests && !forbiddenPaint,
        "Browser switching stays within hidden private launcher controls");
  std::cout << "BROWSER_SWITCH oldOpenTab=" << oldOpenTab
            << " oldClosedTab=" << oldClosedTab << '\n';
}

static void NativeSessionContinuityCases(bool before) {
  Seed();
  g_bUiEnabled = true;
  SelectSessionTab(0, false);
  SetClientInputText(L"Acme support");
  SetClientInputSelection(4, 4);
  const unsigned initialFocusRequests = focusRequests;
  OnSessionEnded(bravoPid);
  DWORD selectionStart = 0, selectionEnd = 0;
  SendMessageW(g_hClientEdit, EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart),
               reinterpret_cast<LPARAM>(&selectionEnd));
  const bool exitReplacedSelection = selectionStart == 0 && selectionEnd == 12;
  const bool exitRequestedFocus = focusRequests > initialFocusRequests;
  Check(GetClientInputText() == L"Acme support" && g_iSelectedSessionTab == 0 &&
            g_sessions.size() == 2,
        "An unrelated browser exit removes only that session and preserves the New query");
  Check(before ? exitReplacedSelection : selectionStart == 4 && selectionEnd == 4,
        "An unrelated exit preserves the current client query caret");
  Check(exitRequestedFocus == before,
        "An unrelated exit does not request focus from the current editor");

  Seed();
  const auto bravo = g_sDataDir / L"Sites" / L"Bravo";
  fs::create_directories(bravo);
  WriteBrowserSlotFixture(bravo / kClientSchemaMarkerName, kClientSchemaMarkerText);
  SelectSessionTab(2, false);
  OnSessionEnded(alphaPid);
  Check(g_iSelectedSessionTab == 1 && g_sessions.front().pid == bravoPid &&
            GetClientInputText() == L"Bravo",
        "The selected browser session stays selected when an earlier tab exits");
  OnSessionEnded(bravoPid);
  wchar_t primaryAction[16]{};
  GetWindowTextW(g_hBtnGo, primaryAction, static_cast<int>(std::size(primaryAction)));
  Check(g_iSelectedSessionTab == 0 && std::wstring_view(primaryAction) == L"Open" &&
            GetClientInputText() == L"Bravo",
        "Closing the selected session keeps its client ready to open again");

  Seed();
  g_sessions[1].clientName = L"Alpha";
  auto chrome = g_activeProfiles.extract(ClientBrowserKey{L"Bravo", BrowserKind::Chrome});
  chrome.key().clientName = L"Alpha";
  g_activeProfiles.insert(std::move(chrome));
  const std::wstring edgeLabel = L"Alpha (Microsoft Edge)";
  const std::wstring chromeLabel = L"Alpha (Google Chrome)";
  std::vector<std::wstring> labels;
  popupInspect = [&](HMENU menu) {
    for (int index = 0; index < GetMenuItemCount(menu); ++index) {
      MENUITEMINFOW item{sizeof(item)};
      item.fMask = MIIM_FTYPE | MIIM_DATA;
      if (GetMenuItemInfoW(menu, index, TRUE, &item) &&
          !(item.fType & MFT_SEPARATOR) && item.dwItemData) {
        labels.push_back(reinterpret_cast<const MenuItemData *>(item.dwItemData)->text);
      }
    }
  };
  menuMutation = {};
  chosenLabel.clear();
  ShowSessionOverflowMenu(g_hGui);
  popupInspect = {};
  const bool identicalOverflow = labels == std::vector<std::wstring>{
      L"Alpha", L"Alpha", L"Charlie", L"Close Alpha", L"Close Alpha", L"Close Charlie"};
  Check(before ? identicalOverflow : labels == std::vector<std::wstring>{
            edgeLabel, chromeLabel, L"Charlie", L"Close " + edgeLabel,
            L"Close " + chromeLabel, L"Close Charlie"},
        "Overflow distinguishes same-client browser sessions and keeps unique names compact");
  if (!before) {
    chosenLabel = chromeLabel;
    ShowSessionOverflowMenu(g_hGui);
    Check(g_iSelectedSessionTab == 2 && g_sessions[g_iSelectedSessionTab-1].pid == bravoPid,
          "The Chrome label shows the Chrome slot for the shared client name");
    chosenLabel = L"Close " + edgeLabel;
    ShowSessionOverflowMenu(g_hGui);
    Check(closeTargets == std::vector<DWORD>{alphaPid} && g_iSelectedSessionTab == 2,
          "The Edge label closes only Edge and keeps the Chrome slot selected");
    g_overflowSessionTabs = {2, 3};
    labels.clear();
    popupInspect = [&](HMENU menu) {
      MENUITEMINFOW item{sizeof(item)};
      item.fMask = MIIM_DATA;
      if (GetMenuItemInfoW(menu, 0, TRUE, &item) && item.dwItemData)
        labels.push_back(reinterpret_cast<const MenuItemData *>(item.dwItemData)->text);
    };
    chosenLabel.clear();
    ShowSessionOverflowMenu(g_hGui);
    popupInspect = {};
    Check(labels == std::vector<std::wstring>{chromeLabel},
          "A hidden slot identifies its browser when its same-client peer is visible");
  }
  Check(!launchRequests && !forbiddenPaint,
        "Session continuity stays within the hidden windows and modeled browser processes");
  std::cout << "SESSION_CONTINUITY exitReplacedSelection=" << exitReplacedSelection
            << " exitRequestedFocus=" << exitRequestedFocus
            << " identicalOverflow=" << identicalOverflow << '\n';
}

static void NativeLauncherOpenCases(bool before) {
  const std::wstring client = L"Echo";
  const auto clients = g_sDataDir / L"Sites";
  const auto clientRoot = clients / client;
  fs::create_directories(clientRoot);
  WriteBrowserSlotFixture(clientRoot / kClientSchemaMarkerName, kClientSchemaMarkerText);
  CreateBrowserSlotFixture(clientRoot, BrowserKind::Edge);
  CreateBrowserSlotFixture(clientRoot, BrowserKind::Chrome);
  g_allClientNames = {client};

  const auto reset = [&] {
    ++scenarios;
    g_sessions = {{client, BrowserKind::Edge, alphaPid},
                  {client, BrowserKind::Chrome, bravoPid}};
    g_activeProfiles.clear();
    for (const auto &session : g_sessions) {
      auto lifetime = std::shared_ptr<void>(
          reinterpret_cast<void *>(static_cast<uintptr_t>(session.pid)), [](void *) {});
      g_activeProfiles.emplace(
          ClientBrowserKey{session.clientName, session.browser},
          ActiveProfileInfo{session.pid, {}, {}, ProfileType::Standard, std::move(lifetime)});
    }
    g_pinnedClients = {L"Alpha", L"Bravo", L"Charlie", L"Delta", client};
    g_selectedBrowser = BrowserKind::Edge;
    g_iSelectedSessionTab = 0;
    g_isLaunchInFlight = false;
    SetUiState(true);
    SetClientInputText(L"");
    fixtureBrowserWindowsReady = true;
    foregroundRequest = nullptr;
    alreadyRunningNotices = 0;
  };
  const auto open = [] {
    WndProc(g_hGui, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED),
            reinterpret_cast<LPARAM>(g_hBtnGo));
  };
  const auto selected = [](size_t tab, DWORD pid, BrowserKind browser) {
    return g_iSelectedSessionTab == static_cast<int>(tab) &&
           g_sessions[tab - 1].pid == pid && g_sessions[tab - 1].browser == browser;
  };

  reset();
  g_selectedBrowser = BrowserKind::Chrome;
  SetClientInputText(L"eCHO");
  UpdatePinButtonState();
  open();
  const bool alreadyRunning = alreadyRunningNotices == 1;
  Check(before ? alreadyRunning && g_iSelectedSessionTab == 0 && !foregroundRequest
               : !alreadyRunning && selected(2, bravoPid, BrowserKind::Chrome) &&
                     foregroundRequest == fixtureBrowserWindows[1] &&
                     GetClientInputText() == client,
        "Open from New shows the existing session for the typed client and selected browser");
  Check(g_bUiEnabled && IsWindowEnabled(g_hBtnGo) && g_sessions.size() == 2 &&
            g_activeProfiles.size() == 2 && g_selectedBrowser == BrowserKind::Chrome,
        "Opening an existing session preserves both independent slots and the launch preference");

  reset();
  SetClientInputText(client);
  g_isLaunchInFlight = true;
  SetUiState(false);
  open();
  const bool busyOpenNotice = alreadyRunningNotices == 1;
  SelectSessionTab(1, false);
  open();
  const bool busyShowRequested = foregroundRequest == fixtureBrowserWindows[0];
  Check(busyOpenNotice == before && busyShowRequested == before,
        "A queued Open or Show cannot interrupt a launch already in progress");
  Check(g_isLaunchInFlight && !g_bUiEnabled && !IsWindowEnabled(g_hBtnGo) &&
            g_sessions.size() == 2,
        "The in-flight launch retains the disabled launcher and both session tabs");
  g_isLaunchInFlight = false;
  SetUiState(true);

  reset();
  chosenLabel = client;
  const auto changeBrowser = [] {
    WndProc(g_hGui, WM_COMMAND, MAKEWPARAM(IDM_BROWSER_CHROME, 0), 0);
  };
  menuMutation = changeBrowser;
  ShowPinnedOverflowMenu(g_hGui, false);
  const bool overflowChangedBrowser = selected(2, bravoPid, BrowserKind::Chrome) &&
                                      foregroundRequest == fixtureBrowserWindows[1];
  Check(g_selectedBrowser == BrowserKind::Chrome &&
            (before ? overflowChangedBrowser
                    : g_iSelectedSessionTab == 0 && !foregroundRequest && GetClientInputText().empty()),
        "Pinned overflow Open never uses a browser selected after the menu opened");

  reset();
  menuMutation = {};
  ShowPinnedOverflowMenu(g_hGui, false);
  Check(selected(1, alphaPid, BrowserKind::Edge) &&
            foregroundRequest == fixtureBrowserWindows[0],
        "Pinned overflow Open still shows the correct session when the browser stays the same");

  reset();
  menuMutation = changeBrowser;
  ShowPinnedOverflowMenu(g_hGui, true);
  Check(g_selectedBrowser == BrowserKind::Chrome && g_iSelectedSessionTab == 0 &&
            GetClientInputText() == client && !foregroundRequest,
        "Pinned overflow Select remains available because it does not choose a browser");
  menuMutation = {};
  chosenLabel.clear();
  Check(!launchRequests && !forbiddenPaint && closeTargets.empty() && terminationTargets.empty(),
        "Launcher Open cases perform no real launch, close, or desktop repaint");
  std::cout << "LAUNCHER_OPEN alreadyRunning=" << alreadyRunning
            << " busyOpenNotice=" << busyOpenNotice
            << " busyShowRequested=" << busyShowRequested
            << " overflowChangedBrowser=" << overflowChangedBrowser << '\n';
}

static void NativeSessionShowPointerCases(bool before) {
  const auto prepare = [] {
    Seed();
    g_bUiEnabled = true;
    g_rcSessionTabs = {0, 0, 600, 32};
    BuildSessionTabRects(g_hGui, nullptr);
    foregroundRequest = nullptr;
    fixtureCapture = nullptr;
  };
  const auto tabPoint = [](int index) {
    const auto &tab = g_sessionTabRects.at(index);
    const POINT point{tab.left + 4, (tab.top + tab.bottom) / 2};
    if (HitTestSessionTab(g_hGui, point) != index || HitTestSessionClose(g_hGui, point) >= 0)
      throw std::runtime_error("Private session click point is not in the tab body");
    return point;
  };
  const auto pointer = [](UINT message, POINT point) {
    WndProc(g_hGui, message, message == WM_LBUTTONDOWN ? MK_LBUTTON : 0,
            MAKELPARAM(point.x, point.y));
  };
  const auto opened = [](DWORD pid, HWND window) {
    return g_iSelectedSessionTab > 0 &&
           g_sessions.at(g_iSelectedSessionTab - 1).pid == pid &&
           foregroundRequest == window;
  };
  const auto stayedOnNew = [] {
    return g_iSelectedSessionTab == 0 && !foregroundRequest;
  };

  prepare();
  pointer(WM_LBUTTONUP, tabPoint(2));
  const bool releaseOnly = opened(bravoPid, fixtureBrowserWindows[1]);
  Check(before ? releaseOnly : stayedOnNew(),
        "A release without a tab press does not show a browser");

  prepare();
  pointer(WM_LBUTTONDOWN, tabPoint(1));
  Check(fixtureCapture == g_hGui, "Pressing a session body captures the private launcher");
  pointer(WM_LBUTTONUP, tabPoint(2));
  const bool adjacentRelease = opened(bravoPid, fixtureBrowserWindows[1]);
  Check(before ? adjacentRelease : stayedOnNew(),
        "Pressing one session and releasing over another does not show either browser");

  prepare();
  const auto oldPosition = tabPoint(2);
  pointer(WM_LBUTTONDOWN, oldPosition);
  fixtureCapture = nullptr;
  WndProc(g_hGui, WM_CAPTURECHANGED, 0, 0);
  OnSessionEnded(alphaPid);
  Check(HitTestSessionTab(g_hGui, oldPosition) == 2 && g_sessions.at(1).pid == charliePid,
        "A private browser exit moves the next session into the original position");
  pointer(WM_LBUTTONUP, oldPosition);
  const bool captureLost = opened(charliePid, fixtureBrowserWindows[2]);
  Check(before ? captureLost : stayedOnNew(),
        "A release after capture loss cannot show the session that inherited the tab position");

  prepare();
  const auto samePosition = tabPoint(2);
  pointer(WM_LBUTTONDOWN, samePosition);
  pointer(WM_LBUTTONUP, samePosition);
  Check(opened(bravoPid, fixtureBrowserWindows[1]) && !fixtureCapture,
        "A complete click on one session shows the corresponding browser and releases capture");

  prepare();
  SelectSessionTab(2, false);
  const auto newPosition = tabPoint(0);
  pointer(WM_LBUTTONDOWN, newPosition);
  pointer(WM_LBUTTONUP, newPosition);
  Check(stayedOnNew(), "Clicking New continues to select the launcher without showing a browser");

  Check(!fixtureCapture && g_mainDragKind == MainDragKind::None &&
            closeTargets.empty() && terminationTargets.empty() && !launchRequests && !forbiddenPaint,
        "Session click tests use only private windows and modeled foreground requests");
  std::cout << "SESSION_SHOW_POINTER releaseOnly=" << releaseOnly
            << " adjacentRelease=" << adjacentRelease
            << " captureLost=" << captureLost << '\n';
}

static void NativePinnedOpenPointerCases(bool before) {
  const std::vector<std::wstring> pins{L"Alpha", L"Bravo", L"Charlie"};
  for (const auto &pin : pins) {
    const auto client = g_sDataDir / L"Sites" / pin;
    fs::create_directories(client);
    WriteBrowserSlotFixture(client / kClientSchemaMarkerName, kClientSchemaMarkerText);
  }

  const auto prepare = [&] {
    Seed();
    g_pinnedClients = pins;
    g_archivedClients.clear();
    g_selectedBrowser = BrowserKind::Edge;
    for (auto &session : g_sessions)
      session.browser = BrowserKind::Edge;
    g_rcPinnedArea = {12, 42, 585, 82};
    g_rcSessionTabs = {0, 0, 600, 32};
    BuildPinnedClientRects();
    BuildSessionTabRects(g_hGui, nullptr);
    SetUiState(true);
    foregroundRequest = nullptr;
    fixtureCapture = nullptr;
  };
  const auto pinPoint = [](int index) {
    const auto &rect = g_pinnedClientRects.at(index);
    return POINT{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
  };
  const auto pointer = [](UINT message, POINT point) {
    WndProc(g_hGui, message, message == WM_LBUTTONDOWN ? MK_LBUTTON : 0,
            MAKELPARAM(point.x, point.y));
  };
  const auto openedBravo = [] {
    return g_iSelectedSessionTab == 2 && GetClientInputText() == L"Bravo" &&
           foregroundRequest == fixtureBrowserWindows[1];
  };
  const auto noOpen = [] {
    return g_iSelectedSessionTab == 0 && GetClientInputText().empty() &&
           !foregroundRequest && !fixtureCapture;
  };

  prepare();
  pointer(WM_LBUTTONUP, pinPoint(1));
  const bool releaseOnly = openedBravo();
  Check(before ? releaseOnly : noOpen(),
        "A release without a matching press cannot open a pinned client");

  prepare();
  pointer(WM_LBUTTONDOWN, pinPoint(0));
  Check(fixtureCapture == g_hGui, "Pressing a pin captures the private launcher");
  pointer(WM_LBUTTONUP, pinPoint(1));
  const bool adjacentRelease = openedBravo();
  Check(before ? adjacentRelease : noOpen(),
        "Pressing one pin and releasing on its neighbor opens neither browser");

  prepare();
  const auto originalPosition = pinPoint(0);
  pointer(WM_LBUTTONDOWN, originalPosition);
  fixtureCapture = nullptr;
  WndProc(g_hGui, WM_CAPTURECHANGED, 0, 0);
  g_pinnedClients.erase(g_pinnedClients.begin());
  BuildPinnedClientRects();
  Check(HitTestPinnedClient(originalPosition) == 0 && g_pinnedClients.front() == L"Bravo",
        "Removing a prior private pin moves Bravo into its position");
  pointer(WM_LBUTTONUP, originalPosition);
  const bool captureLost = openedBravo();
  Check(before ? captureLost : noOpen(),
        "A release after capture loss cannot open the pin that inherited the position");

  prepare();
  SelectSessionTab(2, false);
  const auto newTab = g_sessionTabRects.front();
  pointer(WM_LBUTTONDOWN, pinPoint(0));
  pointer(WM_LBUTTONUP, POINT{newTab.left + 4, (newTab.top + newTab.bottom) / 2});
  const bool switchedToNew = g_iSelectedSessionTab == 0;
  Check(before ? switchedToNew : g_iSelectedSessionTab == 2 && !foregroundRequest,
        "A gesture that starts on a pin cannot select the New tab on release");

  prepare();
  const auto samePosition = pinPoint(1);
  pointer(WM_LBUTTONDOWN, samePosition);
  pointer(WM_LBUTTONUP, samePosition);
  Check(openedBravo() && !fixtureCapture,
        "A complete click on the same pin shows its original browser and releases capture");

  prepare();
  pointer(WM_LBUTTONDOWN, samePosition);
  SetUiState(false);
  pointer(WM_LBUTTONUP, samePosition);
  Check(noOpen() && !g_bUiEnabled, "A disabled launcher ignores a pin release and clears capture");
  SetUiState(true);

  Check(g_mainDragKind == MainDragKind::None && g_pinnedClients == pins &&
            closeTargets.empty() && terminationTargets.empty() && !launchRequests && !forbiddenPaint,
        "Pinned click tests use only private windows and modeled foreground requests");
  std::cout << "PINNED_OPEN_POINTER releaseOnly=" << releaseOnly
            << " adjacentRelease=" << adjacentRelease
            << " captureLost=" << captureLost
            << " switchedToNew=" << switchedToNew << '\n';
}

static POINT LauncherFixtureCenter(RECT rect) {
  if (rect.right <= rect.left || rect.bottom <= rect.top)
    throw std::runtime_error("Private launcher case requires a visible control rectangle");
  return {(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
}

static void NativePaintedControlPointerCases(bool before) {
  const auto prepare = [] {
    Seed();
    g_selectedBrowser = BrowserKind::Edge;
    g_pinnedClients = {L"Alpha", L"Bravo", L"Charlie", L"Delta", L"Echo"};
    g_rcPinnedArea = {12, 42, 520, 82};
    g_rcSessionTabs = {0, 0, 340, 32};
    g_rcBrowserSelector = {355, 4, 450, 30};
    SelectSessionTab(1, false);
    BuildSessionTabRects(g_hGui, nullptr);
    BuildPinnedClientRects();
    SetUiState(true);
    foregroundRequest = nullptr;
    chosenLabel.clear();
  };
  const auto send = [](UINT message, POINT point) {
    WndProc(g_hGui, message, message == WM_LBUTTONDOWN ? MK_LBUTTON : 0,
            MAKELPARAM(point.x, point.y));
  };
  using FixtureRect = const RECT *(*)();
  const std::array<FixtureRect, 4> rectangles{
    +[]() -> const RECT * { return &g_sessionTabRects.front(); },
    +[]() -> const RECT * { return &g_rcSessionOverflow; },
    +[]() -> const RECT * { return &g_rcBrowserSelector; },
    +[]() -> const RECT * { return &g_rcPinnedOverflow; }
  };
  unsigned releaseOnly = 0;
  for (size_t index = 0; index < rectangles.size(); ++index) {
    prepare();
    const auto target = LauncherFixtureCenter(*rectangles[index]());
    const unsigned calls = menuCalls;
    send(WM_LBUTTONUP, target);
    const bool acted = index == 0 ? g_iSelectedSessionTab == 0 : menuCalls == calls + 1;
    if (acted) ++releaseOnly;
    Check(before ? acted : !acted, "Releasing without a press cannot activate a painted launcher control");

    prepare();
    const unsigned previousMenus = menuCalls;
    send(WM_LBUTTONDOWN, target);
    send(WM_LBUTTONUP, target);
    Check((index == 0 ? g_iSelectedSessionTab == 0 : menuCalls == previousMenus + 1) && !fixtureCapture,
          "A same-control press and release activates the painted control and clears capture");
  }

  prepare();
  const auto newTab = LauncherFixtureCenter(g_sessionTabRects.front());
  const auto browser = LauncherFixtureCenter(g_rcBrowserSelector);
  const unsigned callsBeforeCross = menuCalls;
  send(WM_LBUTTONDOWN, newTab);
  send(WM_LBUTTONUP, browser);
  const bool crossTarget = menuCalls == callsBeforeCross + 1;
  Check((before ? crossTarget : !crossTarget) && g_iSelectedSessionTab == 1 && !fixtureCapture,
        "Pressing New and releasing over the browser selector cannot open either action");

  prepare();
  const unsigned callsBeforeCaptureLoss = menuCalls;
  send(WM_LBUTTONDOWN, browser);
  fixtureCapture = nullptr;
  WndProc(g_hGui, WM_CAPTURECHANGED, 0, 0);
  send(WM_LBUTTONUP, browser);
  const bool captureLost = menuCalls == callsBeforeCaptureLoss + 1;
  Check(before ? captureLost : !captureLost, "Losing capture cancels the pressed browser selector");

  prepare();
  send(WM_LBUTTONDOWN, newTab);
  WndProc(g_hGui, WM_CANCELMODE, 0, 0);
  send(WM_LBUTTONUP, newTab);
  const bool cancelled = g_iSelectedSessionTab == 0;
  Check((before ? cancelled : !cancelled) && !fixtureCapture, "Cancel mode clears the pending New action");

  prepare();
  send(WM_LBUTTONDOWN, browser);
  SetUiState(false);
  SetUiState(true);
  const unsigned callsAfterBusy = menuCalls;
  send(WM_LBUTTONUP, browser);
  const bool busyTransition = menuCalls == callsAfterBusy + 1;
  Check((before ? busyTransition : !busyTransition) && !fixtureCapture,
        "Disabling the launcher cancels its pending action even when it becomes enabled again before release");
  Check(!foregroundRequest && !forbiddenPaint && !launchRequests && closeTargets.empty() && terminationTargets.empty(),
        "Painted controls use private windows and suppressed menu presentation");
  std::cout << "PAINTED_CONTROL_POINTER releaseOnly=" << releaseOnly
            << " crossTarget=" << crossTarget << " captureLost=" << captureLost
            << " cancelled=" << cancelled << " busyTransition=" << busyTransition << '\n';
}

static void NativeAppLauncherRound(unsigned round) {
  const std::vector<std::wstring> pins{L"Alpha", L"Bravo", L"Charlie", L"Delta", L"Echo"};
  const auto clients = g_sDataDir / L"Sites";
  for (const auto &pin : pins) {
    const auto client = clients / pin;
    fs::create_directories(client);
    WriteBrowserSlotFixture(client / kClientSchemaMarkerName, kClientSchemaMarkerText);
  }
  Seed();
  g_pinnedClients = pins;
  g_selectedBrowser = BrowserKind::Edge;
  g_rcPinnedArea = {12, 42, 520, 82};
  g_rcSessionTabs = {0, 0, 600, 32};
  BuildPinnedClientRects();
  BuildSessionTabRects(g_hGui, nullptr);
  SetUiState(true);

  const auto center = LauncherFixtureCenter;
  const auto tab = [](size_t index) {
    const auto rect = g_sessionTabRects.at(index);
    const POINT point{rect.left + 4, (rect.top + rect.bottom) / 2};
    if (HitTestSessionTab(g_hGui, point) != static_cast<int>(index) || HitTestSessionClose(g_hGui, point) >= 0)
      throw std::runtime_error("Private launcher case requires a visible session body");
    return point;
  };
  const auto pointer = [](UINT message, POINT point) {
    WndProc(g_hGui, message, message == WM_LBUTTONUP ? 0 : MK_LBUTTON,
            MAKELPARAM(point.x, point.y));
  };
  const auto click = [&](POINT point) {
    pointer(WM_LBUTTONDOWN, point);
    pointer(WM_LBUTTONUP, point);
  };
  const auto command = [](UINT id) {
    WndProc(g_hGui, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0);
  };
  const auto selectedPid = [] {
    return g_iSelectedSessionTab > 0 ? g_sessions.at(g_iSelectedSessionTab - 1).pid : 0;
  };

  const char *name = nullptr;
  switch (round) {
  case 1:
    name = "scaled-pin-same-client-two-browsers";
    g_uiDpi = 144;
    g_rcPinnedArea.right = 350;
    BuildPinnedClientRects();
    g_sessions[0].clientName = L"Charlie";
    g_sessions[0].browser = BrowserKind::Brave;
    command(IDM_BROWSER_FIREFOX);
    click(center(g_pinnedClientRects.at(2)));
    Check(selectedPid() == charliePid && foregroundRequest == fixtureBrowserWindows[2] &&
              GetClientInputText() == L"Charlie",
          "A scaled pin opens Firefox when the same client already has an earlier Brave session");
    Check(g_sessions[0].pid == alphaPid && g_selectedBrowser == BrowserKind::Firefox,
          "Opening the scaled pin preserves the other browser and chosen launch preference");
    break;
  case 2: {
    name = "pin-order-replaced-during-press";
    const POINT first = center(g_pinnedClientRects.front());
    pointer(WM_LBUTTONDOWN, first);
    std::swap(g_pinnedClients[0], g_pinnedClients[1]);
    pointer(WM_LBUTTONUP, first);
    Check(selectedPid() == 0 && !foregroundRequest && GetClientInputText().empty(),
          "A pin moved into the pressed slot cannot open after an independent reorder");
    Check(g_pinnedClients[0] == L"Bravo" && g_pinnedClients[1] == L"Alpha",
          "Canceling the pin gesture preserves the independent reorder");
    break;
  }
  case 3: {
    name = "visible-pin-to-overflow-release";
    const POINT overflow = center(g_rcPinnedOverflow);
    g_sessions[0].clientName = L"Echo";
    chosenLabel = L"Echo";
    pointer(WM_LBUTTONDOWN, center(g_pinnedClientRects.back()));
    pointer(WM_LBUTTONUP, overflow);
    Check(menuCalls == 0 && selectedPid() == 0 && !foregroundRequest,
          "Releasing a visible pin press over overflow cannot open its menu");
    click(overflow);
    Check(menuCalls == 1 && selectedPid() == alphaPid && foregroundRequest == fixtureBrowserWindows[0],
          "A fresh overflow click can show the original hidden pin's browser");
    break;
  }
  case 4: {
    name = "pin-drag-persist-and-reload";
    if (!SavePinnedClients())
      throw std::runtime_error("Cannot write private pin order before dragging");
    const POINT lastVisible = center(g_pinnedClientRects.back());
    pointer(WM_LBUTTONDOWN, center(g_pinnedClientRects.front()));
    pointer(WM_MOUSEMOVE, lastVisible);
    pointer(WM_LBUTTONUP, lastVisible);
    const std::vector<std::wstring> expected{L"Bravo", L"Charlie", L"Delta", L"Alpha", L"Echo"};
    Check(g_pinnedClients == expected && selectedPid() == 0 && !foregroundRequest,
          "Moving a pin past three neighbors changes order without opening the moved pin");
    LoadPinnedClients();
    Check(g_pinnedClients == expected, "Reloading preferences preserves the completed pin order");
    break;
  }
  case 5: {
    name = "session-drag-retains-other-selection";
    SelectSessionTab(2, false);
    const auto last = tab(3);
    pointer(WM_LBUTTONDOWN, tab(1));
    pointer(WM_MOUSEMOVE, last);
    pointer(WM_LBUTTONUP, last);
    Check(g_sessions[0].pid == bravoPid && g_sessions[2].pid == alphaPid,
          "A drag moves the first session to the last tab position");
    Check(selectedPid() == bravoPid && GetClientInputText() == L"Bravo" && !foregroundRequest,
          "Reordering an unselected session retains the selected browser without showing a window");
    break;
  }
  case 6:
    name = "session-exit-during-active-drag";
    pointer(WM_LBUTTONDOWN, tab(1));
    pointer(WM_MOUSEMOVE, tab(2));
    OnSessionEnded(alphaPid);
    pointer(WM_LBUTTONUP, tab(2));
    Check(g_sessions.size() == 2 && g_sessions[0].pid == bravoPid && g_sessions[1].pid == charliePid,
          "A dragged browser exit leaves the other sessions in order");
    Check(selectedPid() == 0 && !foregroundRequest,
          "Releasing after the dragged browser exits does not show its neighbor");
    break;
  case 7:
    name = "browser-switch-case-insensitive-same-client";
    g_sessions[0].browser = BrowserKind::Firefox;
    g_sessions[1].browser = BrowserKind::Brave;
    g_sessions[2].clientName = L"Bravo";
    g_selectedBrowser = BrowserKind::Brave;
    SetClientInputText(L"bRAVo");
    command(IDM_BROWSER_FIREFOX);
    Check(selectedPid() == charliePid && GetClientInputText() == L"Bravo" && !foregroundRequest,
          "The selector finds the typed client's Firefox slot beyond an unrelated Firefox session");
    g_selectedBrowser = BrowserKind::Edge;
    LoadBrowserPreference();
    Check(g_selectedBrowser == BrowserKind::Firefox, "Reloading preferences restores the Firefox choice");
    break;
  case 8:
    name = "switch-to-empty-brave-slot";
    CreateBrowserSlotFixture(clients / L"Alpha", BrowserKind::Edge);
    g_sessions[1].browser = BrowserKind::Brave;
    SelectSessionTab(1, false);
    command(IDM_BROWSER_BRAVE);
    Check(selectedPid() == 0 && g_selectedBrowser == BrowserKind::Brave && GetClientInputText() == L"Alpha",
          "An unrelated Brave session does not become selected when the current client has no Brave slot");
    Check(!g_bRestoreTabsToggleAvailable && !IsWindowEnabled(g_hBtnRestoreTabs) &&
              !HasExistingBrowserSlot(L"Alpha", BrowserKind::Brave) && !foregroundRequest,
          "Selecting an empty Brave slot leaves Restore tabs unavailable and the profile absent");
    break;
  case 9: {
    name = "busy-pin-and-restore-control";
    CreateBrowserSlotFixture(clients / L"Alpha", BrowserKind::Edge);
    SetClientInputText(L"Alpha");
    UpdateRestoreTabsToggleState();
    const POINT first = center(g_pinnedClientRects.front());
    pointer(WM_LBUTTONDOWN, first);
    SetUiState(false);
    pointer(WM_LBUTTONUP, first);
    command(IDC_BTN_RESTORE_TABS);
    Check(selectedPid() == 0 && !foregroundRequest && ShouldRestoreTabsForClient(L"Alpha", BrowserKind::Edge),
          "While disabled, a queued pin release and Restore tabs command leave the client state alone");
    SetUiState(true);
    command(IDC_BTN_RESTORE_TABS);
    click(first);
    Check(selectedPid() == alphaPid && foregroundRequest == fixtureBrowserWindows[0] &&
              !ShouldRestoreTabsForClient(L"Alpha", BrowserKind::Edge),
          "Once idle, the same pin shows Edge and the existing Restore tabs preference can change");
    break;
  }
  case 10:
    name = "pinned-browser-differs-from-selected-session";
    CreateBrowserSlotFixture(clients / L"Bravo", BrowserKind::Chrome);
    CreateBrowserSlotFixture(clients / L"Bravo", BrowserKind::Firefox);
    g_sessions[2].clientName = L"Bravo";
    g_selectedBrowser = BrowserKind::Firefox;
    SelectSessionTab(2, false);
    command(IDC_BTN_RESTORE_TABS);
    click(center(g_pinnedClientRects.at(1)));
    LoadRestoreTabsPreferences();
    Check(selectedPid() == charliePid && foregroundRequest == fixtureBrowserWindows[2],
          "A pin uses the launcher's Firefox preference while the client's Chrome tab was selected");
    Check(!ShouldRestoreTabsForClient(L"Bravo", BrowserKind::Chrome) &&
              ShouldRestoreTabsForClient(L"Bravo", BrowserKind::Firefox),
          "The selected tab's Restore tabs command persists only the Chrome setting");
    break;
  default:
    throw std::runtime_error("Unexpected private launcher round");
  }

  Check(!fixtureCapture && g_mainDragKind == MainDragKind::None && !launchRequests && !forbiddenPaint &&
            closeTargets.empty() && terminationTargets.empty() && !suppressedClipboardRequests,
        "Launcher round keeps capture, browser windows, clipboard and preferences inside the fixture");
  std::cout << "APP_LAUNCHER_ROUND round=" << round << " case=" << name << '\n';
}
