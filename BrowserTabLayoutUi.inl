// Browser tab layout is a launch preference, separate from Restore tabs.
// Client overrides live at the client root, so client lifecycle operations
// carry them without name-indexed preferences or cross-client reconciliation.
static bool IsGlobalTabLayoutCommand(UINT command) {
  return command == IDM_TAB_LAYOUT_GLOBAL_BROWSER ||
         command == IDM_TAB_LAYOUT_GLOBAL_VERTICAL ||
         command == IDM_TAB_LAYOUT_GLOBAL_HORIZONTAL;
}

static bool IsClientTabLayoutCommand(UINT command) {
  return command == IDM_TAB_LAYOUT_CLIENT_FOLLOW ||
         command == IDM_TAB_LAYOUT_CLIENT_VERTICAL ||
         command == IDM_TAB_LAYOUT_CLIENT_HORIZONTAL;
}

static void LoadGlobalTabLayoutPreference() {
  g_globalTabLayout = client_tab_layout::Layout::Browser;
  if (g_sConfigPath.empty() || !HasReadableConfigFile())
    return;
  wchar_t value[32]{};
  GetPrivateProfileStringW(L"user", L"tab_layout", L"browser", value,
                           static_cast<DWORD>(std::size(value)),
                           g_sConfigPath.c_str());
  if (_wcsicmp(value, L"vertical") == 0)
    g_globalTabLayout = client_tab_layout::Layout::Vertical;
  else if (_wcsicmp(value, L"horizontal") == 0)
    g_globalTabLayout = client_tab_layout::Layout::Horizontal;
}

static bool SetGlobalTabLayoutPreference(client_tab_layout::Layout layout) {
  if (!g_bUiEnabled || g_isLaunchInFlight.load() ||
      g_bCleanupBusy.load() || g_bCloseAllClientsPending)
    return false;
  const wchar_t *value = nullptr;
  switch (layout) {
  case client_tab_layout::Layout::Browser: value = L"browser"; break;
  case client_tab_layout::Layout::Vertical: value = L"vertical"; break;
  case client_tab_layout::Layout::Horizontal: value = L"horizontal"; break;
  default: return false;
  }
  if (!SaveConfigMutations(
          L"all-client tab layout",
          {{L"user", std::wstring(L"tab_layout"), std::wstring(value)}}))
    return false;
  g_globalTabLayout = layout;
  return true;
}

static bool SetClientTabLayoutPreference(
    const std::wstring &clientName, client_tab_layout::Override choice) {
  if (!g_bUiEnabled || g_isLaunchInFlight.load() ||
      g_bCleanupBusy.load() || g_bCloseAllClientsPending ||
      !IsExistingClientProfile(clientName) || IsClientArchived(clientName))
    return false;
  fs::path root;
  std::wstring error;
  if (TryGetSafeClientProfilePath(clientName, root) &&
      client_tab_layout::Write(root, choice, &error))
    return true;
  std::wstring message =
      L"This client's tab-layout choice could not be saved safely. "
      L"Its saved setting and other clients were left unchanged.";
  if (!error.empty()) message += L"\n\n" + error;
  MessageBoxW(g_hGui, message.c_str(), L"Tab Layout Was Not Saved",
               MB_OK | MB_ICONWARNING);
  return false;
}

static bool ApplyBrowserTabLayoutPreference(
    const fs::path &profilePath, BrowserKind browser,
    client_tab_layout::Layout layout, std::wstring &errorDetails) {
  errorDetails.clear();
  if (browser == BrowserKind::Firefox) {
    std::optional<bool> vertical;
    if (layout == client_tab_layout::Layout::Vertical) vertical = true;
    else if (layout == client_tab_layout::Layout::Horizontal) vertical = false;
    else if (layout != client_tab_layout::Layout::Browser) return false;
    return firefox_preferences::UpdateTabLayoutPreferenceFile(
        profilePath, vertical, &errorDetails);
  }
  if (layout == client_tab_layout::Layout::Browser)
    return true;
  if (layout != client_tab_layout::Layout::Vertical &&
      layout != client_tab_layout::Layout::Horizontal)
    return false;
  browser_preferences::TabLayoutBrowser target;
  switch (browser) {
  case BrowserKind::Edge: target = browser_preferences::TabLayoutBrowser::Edge; break;
  case BrowserKind::Chrome: target = browser_preferences::TabLayoutBrowser::Chrome; break;
  case BrowserKind::Brave: target = browser_preferences::TabLayoutBrowser::Brave; break;
  default: return false;
  }
  const auto result = browser_preferences::UpdateTabLayoutPreferencesFile(
      profilePath / L"Default" / L"Preferences", target,
      layout == client_tab_layout::Layout::Vertical, &errorDetails);
  return result == browser_preferences::FileUpdateResult::unchanged ||
         result == browser_preferences::FileUpdateResult::updated;
}
