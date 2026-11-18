static LRESULT CALLBACK RouteFixtureBrowserCommand(HWND window, UINT message, WPARAM wParam,
                                                  LPARAM lParam, UINT_PTR id, DWORD_PTR) {
  if (message == WM_NCDESTROY)
    RemoveWindowSubclass(window, RouteFixtureBrowserCommand, id);
  if (message == WM_COMMAND && HIWORD(wParam) == 0 && lParam == 0) {
    switch (LOWORD(wParam)) {
    case IDM_BROWSER_EDGE:
    case IDM_BROWSER_CHROME:
    case IDM_BROWSER_BRAVE:
    case IDM_BROWSER_FIREFOX:
      return WndProc(window, message, wParam, lParam);
    }
  }
  return DefSubclassProc(window, message, wParam, lParam);
}

static void NativePaintedAppLauncherRound(unsigned round) {
  // The fixture owns a hidden STATIC parent. Route only the synchronous
  // browser command emitted by the actual selector menu through WndProc.
  if (!SetWindowSubclass(g_hGui, RouteFixtureBrowserCommand, 1, 0))
    throw std::runtime_error("Cannot route private browser commands");
  g_sEdgePath = g_sDataDir / L"modeled-edge.exe";
  g_sChromePath = g_sDataDir / L"modeled-chrome.exe";
  g_sBravePath = g_sDataDir / L"modeled-brave.exe";
  g_sFirefoxPath = g_sDataDir / L"modeled-firefox.exe";
  const auto clients = g_sDataDir / L"Sites";
  g_pinnedClients = {L"Alpha", L"Bravo", L"Charlie", L"Delta", L"Echo"};
  for (const auto &pin : g_pinnedClients) {
    const auto client = clients / pin;
    fs::create_directories(client);
    WriteBrowserSlotFixture(client / kClientSchemaMarkerName, kClientSchemaMarkerText);
  }
  Seed();
  g_rcPinnedArea = {12, 42, 520, 82};
  g_rcSessionTabs = {0, 0, 340, 32};
  g_rcBrowserSelector = {14, 196, 168, 224};
  g_selectedBrowser = BrowserKind::Edge;
  SelectSessionTab(3, false);
  SetUiState(true);
  BuildPinnedClientRects();
  BuildSessionTabRects(g_hGui, nullptr);

  const auto center = LauncherFixtureCenter;
  const auto browser = center(g_rcBrowserSelector);
  const auto pinnedOverflow = center(g_rcPinnedOverflow);
  const auto sessionOverflow = center(g_rcSessionOverflow);
  const auto newTab = center(g_sessionTabRects.front());
  const auto pointer = [](UINT message, POINT point) {
    WndProc(g_hGui, message, message == WM_LBUTTONDOWN ? MK_LBUTTON : 0,
            MAKELPARAM(point.x, point.y));
  };
  const auto click = [&](POINT point) {
    pointer(WM_LBUTTONDOWN, point);
    pointer(WM_LBUTTONUP, point);
  };
  const auto selected = [] {
    return g_iSelectedSessionTab > 0 ? g_sessions.at(g_iSelectedSessionTab - 1).pid : 0;
  };

  const char *name = nullptr;
  switch (round) {
  case 1:
    name = "selector-press-to-session-overflow";
    pointer(WM_LBUTTONDOWN, browser);
    pointer(WM_LBUTTONUP, sessionOverflow);
    Check(!menuCalls && selected() == charliePid && g_selectedBrowser == BrowserKind::Edge,
          "A browser-selector press cannot open session overflow on release");
    chosenLabel = L"Alpha";
    click(sessionOverflow);
    Check(menuCalls == 1 && selected() == alphaPid && foregroundRequest == fixtureBrowserWindows[0],
          "A fresh overflow click opens exactly the chosen hidden Edge browser");
    break;
  case 2:
    name = "browser-popup-selects-existing-chrome";
    g_sessions[1].clientName = L"Charlie";
    chosenLabel = L"Google Chrome";
    click(browser);
    Check(menuCalls == 1 && selected() == bravoPid && !foregroundRequest &&
              GetClientInputText() == L"Charlie" && g_selectedBrowser == BrowserKind::Chrome,
          "The actual selector popup picks the current client's existing Chrome tab");
    g_selectedBrowser = BrowserKind::Edge;
    LoadBrowserPreference();
    Check(g_selectedBrowser == BrowserKind::Chrome, "Reloading preferences keeps the actual popup selection");
    break;
  case 3:
    name = "session-overflow-disappears-on-resize";
    pointer(WM_LBUTTONDOWN, sessionOverflow);
    g_rcSessionTabs.right = 600;
    BuildSessionTabRects(g_hGui, nullptr);
    pointer(WM_LBUTTONUP, sessionOverflow);
    Check(!menuCalls && !foregroundRequest && selected() == charliePid && g_overflowSessionTabs.empty(),
          "Widening the launcher removes overflow and cancels its pending action");
    click(center(g_sessionTabRects.at(1)));
    Check(selected() == alphaPid && foregroundRequest == fixtureBrowserWindows[0],
          "The exposed session remains clickable after overflow disappears");
    break;
  case 4:
    name = "pinned-overflow-disappears-on-unpin";
    pointer(WM_LBUTTONDOWN, pinnedOverflow);
    g_pinnedClients.pop_back();
    BuildPinnedClientRects();
    pointer(WM_LBUTTONUP, pinnedOverflow);
    Check(!menuCalls && selected() == charliePid && !foregroundRequest,
          "Removing the only hidden pin cancels its pending overflow click");
    g_pinnedClients.push_back(L"Echo");
    BuildPinnedClientRects();
    click(center(g_rcPinnedOverflow));
    Check(menuCalls == 1 && selected() == charliePid && !foregroundRequest,
          "Restoring overflow allows a new click and canceling the popup preserves selection");
    break;
  case 5: {
    name = "right-click-pinned-overflow-selects-without-opening";
    chosenLabel = L"Echo";
    pointer(WM_RBUTTONUP, pinnedOverflow);
    Check(menuCalls == 1 && selected() == 0 && GetClientInputText() == L"Echo" && !foregroundRequest,
          "Right-clicking pinned overflow selects a hidden favorite without opening its browser");
    const auto echo = clients / L"Echo";
    Check(!HasExistingBrowserSlot(L"Echo", BrowserKind::Edge) && !fs::exists(echo / L"Browsers"),
          "A select-only pinned action creates no browser data");
    break;
  }
  case 6:
    name = "browser-popup-busy-refusal-and-idle-retry";
    chosenLabel = L"Brave Browser";
    menuMutation = [] {
      g_bClientNotesBusy = true;
      SetUiState(false);
    };
    click(browser);
    Check(menuCalls == 1 && !g_bUiEnabled && g_selectedBrowser == BrowserKind::Edge &&
              selected() == charliePid && !foregroundRequest,
          "A modal operation that begins while the selector is open blocks the pending browser choice");
    menuMutation = {};
    g_bClientNotesBusy = false;
    SetUiState(true);
    click(browser);
    Check(menuCalls == 2 && g_selectedBrowser == BrowserKind::Brave &&
              selected() == 0 && GetClientInputText() == L"Charlie",
          "The same selector choice succeeds once the launcher becomes idle");
    break;
  case 7: {
    name = "browser-popup-config-lock-and-retry";
    if (!SaveBrowserPreference())
      throw std::runtime_error("Cannot seed the private browser configuration");
    const auto before = ReadBrowserSlotSettings();
    HANDLE lock = CreateFileW(g_sConfigPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, 0, nullptr);
    if (lock == INVALID_HANDLE_VALUE)
      throw std::runtime_error("Cannot lock the private browser configuration");
    std::unique_ptr<void, decltype(&CloseHandle)> closeLock(lock, CloseHandle);
    chosenLabel = L"Google Chrome";
    click(browser);
    closeLock.reset();
    Check(g_selectedBrowser == BrowserKind::Edge && selected() == charliePid &&
              ReadBrowserSlotSettings() == before && !foregroundRequest,
          "A locked settings file preserves the browser choice and selected session");
    click(browser);
    Check(g_selectedBrowser == BrowserKind::Chrome && selected() == 0 &&
              ReadBrowserSlotSettings() != before && GetClientInputText() == L"Charlie",
          "Retry publishes the browser choice and keeps the typed client available");
    break;
  }
  case 8:
    name = "paired-browser-overflow-show-after-reorder";
    g_sessions[1].clientName = L"Alpha";
    chosenLabel = L"Alpha (Google Chrome)";
    menuMutation = [] { std::swap(g_sessions[0], g_sessions[1]); };
    click(sessionOverflow);
    Check(menuCalls == 1 && selected() == bravoPid && foregroundRequest == fixtureBrowserWindows[1],
          "The browser label follows its original session when hidden siblings reorder during the popup");
    Check(g_sessions[1].pid == alphaPid && g_selectedBrowser == BrowserKind::Edge && closeTargets.empty(),
          "Showing Chrome keeps its same-client Edge sibling and the launch preference intact");
    break;
  case 9:
    name = "hidden-brave-close-preserves-selected-firefox";
    g_sessions[1].clientName = L"Charlie";
    g_sessions[1].browser = BrowserKind::Brave;
    chosenLabel = L"Close Charlie (Brave Browser)";
    click(sessionOverflow);
    Check(menuCalls == 1 && closeTargets == std::vector<DWORD>{bravoPid} &&
              selected() == charliePid && !foregroundRequest,
          "Closing hidden Brave requests normal close only for Brave and leaves Firefox selected");
    Check(g_sessions.size() == 3 && terminationTargets.empty(),
          "A normal close request waits for the browser exit notification");
    break;
  case 10:
    name = "new-tab-capture-loss-then-browser-popup";
    pointer(WM_LBUTTONDOWN, newTab);
    fixtureCapture = nullptr;
    WndProc(g_hGui, WM_CAPTURECHANGED, 0, 0);
    pointer(WM_LBUTTONUP, newTab);
    Check(selected() == charliePid && !foregroundRequest && !menuCalls,
          "Losing capture over New leaves the active Firefox session selected");
    click(newTab);
    chosenLabel = L"Microsoft Edge";
    click(browser);
    Check(selected() == 0 && menuCalls == 1 && GetClientInputText() == L"Charlie" &&
              g_selectedBrowser == BrowserKind::Edge && !foregroundRequest,
          "A fresh New click and a complete selector click both work after capture loss");
    break;
  default:
    throw std::runtime_error("Unexpected private launcher round");
  }

  Check(!fixtureCapture && g_mainDragKind == MainDragKind::None && !launchRequests && !forbiddenPaint &&
            terminationTargets.empty() && !suppressedClipboardRequests &&
            (round == 9 || closeTargets.empty()),
        "Launcher round keeps capture, browser windows, clipboard and preferences inside the fixture");
  std::cout << "APP_LAUNCHER_ROUND round=" << round << " case=" << name << '\n';
}
