static std::wstring LauncherButtonText(HWND button) {
  wchar_t text[32]{};
  GetWindowTextW(button, text, static_cast<int>(std::size(text)));
  return text;
}

static void SeedInteractionClient(const std::wstring &client,
                                  std::initializer_list<BrowserKind> browsers = {}) {
  const auto root = g_sDataDir / L"Sites" / client;
  fs::create_directories(root);
  WriteBrowserSlotFixture(root / kClientSchemaMarkerName, kClientSchemaMarkerText);
  for (const auto browser : browsers)
    CreateBrowserSlotFixture(root, browser);
}

static LRESULT CALLBACK InteractionEditParent(HWND window, UINT message,
                                               WPARAM wParam, LPARAM lParam,
                                               UINT_PTR, DWORD_PTR data) {
  if (message == WM_COMMAND && HIWORD(wParam) == EN_CHANGE &&
      reinterpret_cast<HWND>(lParam) == g_hClientEdit) {
    ++*reinterpret_cast<unsigned *>(data);
    return WndProc(window, message, MAKEWPARAM(IDC_CLIENT_EDIT, EN_CHANGE), lParam);
  }
  return DefSubclassProc(window, message, wParam, lParam);
}

static void NativeInvalidSessionEdit(bool before) {
  SeedInteractionClient(L"Charlie", {BrowserKind::Edge, BrowserKind::Firefox});
  Seed();
  g_selectedBrowser = BrowserKind::Edge;
  SetUiState(true);
  SelectSessionTab(3, false);
  Check(SelectedFixtureSession() == charliePid && GetClientInputText() == L"Charlie" &&
            LauncherButtonText(g_hBtnGo) == L"Show" &&
            GetBrowserForCurrentSelection() == BrowserKind::Firefox,
        "The private Firefox session starts selected with an Edge launcher default");

  unsigned edits = 0;
  constexpr UINT_PTR subclassId = 0x635001;
  if (!SetWindowSubclass(g_hGui, InteractionEditParent, subclassId,
                         reinterpret_cast<DWORD_PTR>(&edits)))
    throw std::runtime_error("Cannot attach private launcher edit notification routing");
  SendMessageW(g_hClientEdit, EM_SETSEL, 0, -1);
  SendMessageW(g_hClientEdit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"Charlie/"));
  const bool switched = !SelectedFixtureSession() &&
                        LauncherButtonText(g_hBtnGo) == L"Open" &&
                        GetBrowserForCurrentSelection() == BrowserKind::Edge;
  Check(edits > 0 && GetClientInputText() == L"Charlie" &&
            (before ? switched : SelectedFixtureSession() == charliePid &&
                                   LauncherButtonText(g_hBtnGo) == L"Show" &&
                                   GetBrowserForCurrentSelection() == BrowserKind::Firefox),
        before ? "The rejected edit restores Charlie but switches Firefox Show to Edge Open"
               : "The rejected edit restores Charlie and preserves its Firefox Show action");
  if (!before && SelectedFixtureSession() == charliePid) {
    WndProc(g_hGui, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
    Check(foregroundRequest == fixtureBrowserWindows[2],
          "The primary action still targets the selected Firefox browser");
  }

  const unsigned rejected = edits;
  SendMessageW(g_hClientEdit, EM_SETSEL, 0, -1);
  SendMessageW(g_hClientEdit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"New Charlie"));
  RemoveWindowSubclass(g_hGui, InteractionEditParent, subclassId);
  Check(edits > rejected && !SelectedFixtureSession() &&
            GetClientInputText() == L"New Charlie" &&
            LauncherButtonText(g_hBtnGo) == L"Create" &&
            GetBrowserForCurrentSelection() == BrowserKind::Edge,
        "A valid new client name still leaves Show mode and keeps the launcher's Edge default");
  Check(!launchRequests && !forbiddenPaint && closeTargets.empty() && terminationTargets.empty(),
        "The input case uses the hidden native edit and modeled browser without external actions");
  std::cout << "LAUNCHER_INVALID_SESSION_EDIT switched=" << switched << '\n';
}

static void LauncherInteractionCommand(UINT command, UINT notification = BN_CLICKED) {
  WndProc(g_hGui, WM_COMMAND, MAKEWPARAM(command, notification), 0);
}

static void CheckCloseAllPending() {
  Check(g_bCloseAllClientsPending && !g_bUiEnabled &&
            LauncherButtonText(g_hBtnCloseAll) == L"Closing...",
        "An accepted Close all owns the launcher while awaiting the browsers");
}

static void NativeLateCloseAllWindow(bool before) {
  Seed();
  SetUiState(true);
  fixtureBrowserWindowsReady = false;
  unsigned timeoutNotices = 0;
  closeAllResponse = [&](UINT flags) {
    if (flags & MB_YESNO)
      return IDYES;
    ++timeoutNotices;
    return IDOK;
  };
  LauncherInteractionCommand(IDC_BTN_CLOSE_ALL);
  CheckCloseAllPending();
  Check(g_closeAllAwaitingWindows.size() == 3 && closeTargets.empty(),
        "The modeled client processes begin without browser windows to close");

  g_closeAllDeadline = GetTickCount64();
  fixtureBrowserWindowsReady = true;
  WndProc(g_hGui, WM_TIMER, kCloseAllCheckTimer, 0);
  const bool lateClose = closeTargets == std::vector<DWORD>{alphaPid, bravoPid, charliePid};
  Check(before ? lateClose : closeTargets.empty(),
        before ? "A timer tick closes the three windows even though they appeared at the deadline"
               : "Browsers first available at the deadline receive no pending Close request");
  Check(!g_bCloseAllClientsPending && g_closeAllAwaitingWindows.empty() &&
            g_bUiEnabled && LauncherButtonText(g_hBtnCloseAll) == L"Close all" &&
            g_sessions.size() == 3 && timeoutNotices == 1,
        "The expired operation releases its launcher guard and leaves the three sessions available");

  closeTargets.clear();
  LauncherInteractionCommand(IDC_BTN_CLOSE_ALL);
  Check(closeTargets == std::vector<DWORD>{alphaPid, bravoPid, charliePid},
        "A fresh confirmation closes the three now-ready windows through normal requests");
  g_closeAllDeadline = GetTickCount64();
  WndProc(g_hGui, WM_TIMER, kCloseAllCheckTimer, 0);
  closeAllResponse = {};
  Check(!launchRequests && !forbiddenPaint && terminationTargets.empty(),
        "Close all stays inside the modeled windows and never forces termination");
  std::cout << "LAUNCHER_CLOSE_ALL_DEADLINE lateClose=" << lateClose << '\n';
}

static void NativeLauncherInteractionRound(unsigned round) {
  const char *name = nullptr;
  if (round == 1) {
    NativeInvalidSessionEdit(false);
    name = "rejected-edit-preserves-firefox-show";
  } else if (round == 2) {
    NativeLateCloseAllWindow(false);
    name = "close-all-deadline-releases-late-browser";
  } else {
    Seed();
    g_selectedBrowser = BrowserKind::Edge;
    g_rcSessionTabs = {0, 0, 600, 32};
    BuildSessionTabRects(g_hGui, nullptr);
    SetUiState(true);
    switch (round) {
    case 3: {
      name = "close-all-declined-preserves-selected-browser";
      SelectSessionTab(2, false);
      SetWindowTextW(g_hBtnCloseAll, L"Close all");
      unsigned confirmations = 0;
      closeAllResponse = [&](UINT flags) {
        if (flags & MB_YESNO)
          ++confirmations;
        return IDNO;
      };
      LauncherInteractionCommand(IDC_BTN_CLOSE_ALL);
      closeAllResponse = {};
      Check(confirmations == 1 && !g_bCloseAllClientsPending &&
                g_bUiEnabled && SelectedFixtureSession() == bravoPid &&
                LauncherButtonText(g_hBtnGo) == L"Show" &&
                LauncherButtonText(g_hBtnCloseAll) == L"Close all" &&
                closeTargets.empty() && g_closeAllAwaitingWindows.empty(),
            "Declining Close all keeps the selected browser and every close control unchanged");
      break;
    }
    case 4: {
      name = "close-all-resnapshots-after-confirmation";
      SeedInteractionClient(L"Bravo", {BrowserKind::Chrome});
      g_sessions[2].clientName = L"Temp";
      g_sessions[2].browser = BrowserKind::Firefox;
      SelectSessionTab(2, false);
      closeAllResponse = [](UINT flags) {
        if (flags & MB_YESNO) {
          OnSessionEnded(alphaPid);
          return IDYES;
        }
        return IDOK;
      };
      LauncherInteractionCommand(IDC_BTN_CLOSE_ALL);
      CheckCloseAllPending();
      Check(closeTargets == std::vector<DWORD>{bravoPid} &&
                SelectedFixtureSession() == bravoPid && g_sessions.size() == 2,
            "An exit during confirmation leaves only the surviving standard client eligible for Close");
      OnSessionEnded(bravoPid);
      closeAllResponse = {};
      Check(g_bUiEnabled && !g_bCloseAllClientsPending && g_sessions.size() == 1 &&
                g_sessions.front().clientName == L"Temp" && !IsWindowEnabled(g_hBtnCloseAll),
            "The final standard client exit re-enables the launcher and retains Temp");
      break;
    }
    case 5: {
      name = "close-all-deliver-once-after-post-refusal";
      refuseClosePost = true;
      closeAllResponse = [](UINT flags) { return flags & MB_YESNO ? IDYES : IDOK; };
      LauncherInteractionCommand(IDC_BTN_CLOSE_ALL);
      CheckCloseAllPending();
      Check(closeTargets.empty() && g_closeAllAwaitingWindows.size() == 3,
            "A failed normal post queues each modeled client for a retry");
      refuseClosePost = false;
      for (unsigned tick = 0; tick < 2; ++tick)
        WndProc(g_hGui, WM_TIMER, kCloseAllCheckTimer, 0);
      Check(closeTargets == std::vector<DWORD>{alphaPid, bravoPid, charliePid} &&
                g_closeAllAwaitingWindows.empty() && g_bCloseAllClientsPending,
            "A successful retry reaches each browser once and further ticks never repeat it");
      for (DWORD process : {alphaPid, bravoPid, charliePid})
        OnSessionEnded(process);
      closeAllResponse = {};
      Check(g_bUiEnabled && !g_bCloseAllClientsPending && g_sessions.empty(),
            "The last modeled browser exit clears the completed Close all operation");
      break;
    }
    case 6:
      name = "pinned-restore-uses-default-from-another-session";
      SeedInteractionClient(L"Alpha", {BrowserKind::Edge, BrowserKind::Firefox});
      SeedInteractionClient(L"Charlie", {BrowserKind::Firefox});
      SelectSessionTab(3, false);
      chosenLabel = L"Restore tabs";
      ShowPinnedClientOptionsMenu(g_hGui, L"Alpha", {});
      LoadRestoreTabsPreferences();
      Check(menuCalls == 1 && !SelectedFixtureSession() && GetClientInputText() == L"Alpha" &&
                !ShouldRestoreTabsForClient(L"Alpha", BrowserKind::Edge) &&
                ShouldRestoreTabsForClient(L"Alpha", BrowserKind::Firefox) &&
                ShouldRestoreTabsForClient(L"Charlie", BrowserKind::Firefox) && !foregroundRequest,
            "Pinned Restore tabs updates Alpha's Edge slot while leaving both Firefox slots untouched");
      break;
    case 7: {
      name = "settings-maintenance-gates-active-client";
      SeedInteractionClient(L"Alpha", {BrowserKind::Edge});
      SeedInteractionClient(L"Echo");
      const auto available = [](HMENU menu, UINT id) {
        const auto state = GetMenuState(menu, id, MF_BYCOMMAND);
        return state != static_cast<UINT>(-1) && !(state & (MF_GRAYED | MF_DISABLED));
      };
      unsigned inspected = 0;
      const auto inspect = [&](bool expected) {
        popupInspect = [&](HMENU menu) {
          ++inspected;
          Check(available(menu, IDM_CTX_RENAME_PROFILE) == expected &&
                    available(menu, IDM_CTX_ARCHIVE_PROFILE) == expected &&
                    available(menu, IDM_CTX_DELETE_PROFILE) == expected &&
                    available(menu, IDM_CTX_CLIENT_NOTES),
                "Settings updates maintenance eligibility for the current client and leaves Notes available");
        };
        LauncherInteractionCommand(IDC_BTN_CONFIG);
        popupInspect = {};
      };
      SelectSessionTab(1, false);
      inspect(false);
      SelectSessionTab(0, false);
      SetClientInputText(L"Echo");
      inspect(true);
      Check(inspected == 2 && !foregroundRequest,
            "Both private Settings menus were inspected without acting on either client");
      break;
    }
    case 8: {
      name = "client-refresh-filters-archived-and-prunes-its-pin";
      for (const auto *client : {L"Alpha", L"Bravo", L"Charlie"})
        SeedInteractionClient(client);
      g_archivedClients = {L"Bravo"};
      g_pinnedClients = {L"Alpha", L"Bravo", L"Charlie"};
      if (!SaveArchivedClients() || !SavePinnedClients())
        throw std::runtime_error("Cannot initialize private archived or pinned preferences");
      SelectSessionTab(3, false);
      UpdateClientsComboBox();
      LoadPinnedClients();
      Check(g_allClientNames == std::vector<std::wstring>{L"Alpha", L"Charlie"} &&
                g_pinnedClients == std::vector<std::wstring>{L"Alpha", L"Charlie"} &&
                fs::is_directory(g_sDataDir / L"Sites" / L"Bravo"),
            "Refreshing hides the archived client and removes its pin while retaining the client directory");
      Check(GetClientInputText() == L"Charlie" && SelectedFixtureSession() == charliePid &&
                LauncherButtonText(g_hBtnGo) == L"Show" && !foregroundRequest,
            "Refreshing the client list preserves the selected Firefox browser and primary action");
      break;
    }
    case 9: {
      name = "session-start-uses-tracked-browser-over-default";
      SeedInteractionClient(L"Charlie", {BrowserKind::Firefox});
      g_sessions.pop_back();
      const std::wstring delayed = L"Charlie";
      WndProc(g_hGui, WM_APP_SESSION_STARTED, charliePid,
              reinterpret_cast<LPARAM>(new std::wstring(delayed)));
      const auto identity = g_sessions.back().identity;
      WndProc(g_hGui, WM_APP_SESSION_STARTED, charliePid,
              reinterpret_cast<LPARAM>(new std::wstring(delayed)));
      Check(g_sessions.size() == 3 && g_sessions.back().identity == identity &&
                SelectedFixtureSession() == charliePid &&
                GetBrowserForCurrentSelection() == BrowserKind::Firefox &&
                g_selectedBrowser == BrowserKind::Edge && g_bRestoreTabsToggleAvailable && !foregroundRequest,
            "A delayed session start uses its tracked Firefox browser and an identical notification does not duplicate it");
      break;
    }
    case 10: {
      name = "browser-selector-lists-unavailable-browser-disabled";
      if (!SetWindowSubclass(g_hGui, RouteFixtureBrowserCommand, 1, 0))
        throw std::runtime_error("Cannot route private browser commands");
      SeedInteractionClient(L"Charlie", {BrowserKind::Firefox});
      g_sEdgePath = g_sDataDir / L"modeled-edge.exe";
      g_sFirefoxPath = g_sDataDir / L"modeled-firefox.exe";
      g_sBravePath.clear();
      g_sChromePath.clear();
      g_rcBrowserSelector = {12, 100, 160, 130};
      SetClientInputText(L"Charlie");
      chosenLabel = IsGuideFeatureCommandNew(IDM_BROWSER_FIREFOX)
                        ? L"Mozilla Firefox (New)" : L"Mozilla Firefox";
      bool inspected = false;
      popupInspect = [&](HMENU menu) {
        const auto state = [&](UINT command) {
          return GetMenuState(menu, command, MF_BYCOMMAND);
        };
        const auto edge = state(IDM_BROWSER_EDGE);
        const auto brave = state(IDM_BROWSER_BRAVE);
        const auto firefox = state(IDM_BROWSER_FIREFOX);
        inspected = edge != static_cast<UINT>(-1) && brave != static_cast<UINT>(-1) &&
                    firefox != static_cast<UINT>(-1) && (edge & MF_CHECKED) &&
                    (brave & (MF_GRAYED | MF_DISABLED)) && !(firefox & (MF_GRAYED | MF_DISABLED));
      };
      const auto button = LauncherFixtureCenter(g_rcBrowserSelector);
      SendLauncherPointer(WM_LBUTTONDOWN, button);
      SendLauncherPointer(WM_LBUTTONUP, button);
      popupInspect = {};
      Check(inspected && menuCalls == 1 && g_selectedBrowser == BrowserKind::Firefox &&
                SelectedFixtureSession() == charliePid && !foregroundRequest,
            "The browser menu shows the saved choice, grays unavailable Brave and selects the existing Firefox session");
      break;
    }
    default:
      throw std::runtime_error("Unexpected launcher interaction round");
    }
  }
  Check(!fixtureCapture && g_mainDragKind == MainDragKind::None && !launchRequests &&
            !forbiddenPaint && terminationTargets.empty() &&
            suppressedClipboardRequests == (round == 6 ? 1u : 0u),
        "Launcher interaction retains only private windows, configuration and modeled browser lifetimes");
  std::cout << "APP_LAUNCHER_ROUND round=" << round << " case=" << name << '\n';
}
