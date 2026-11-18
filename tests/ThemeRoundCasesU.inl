static void RunThemeRoundU(const fs::path &root, int round) {
  constexpr int initial = 3, favorite = 4;
  ThemeSeed(root, initial);
  auto set = [](std::optional<std::wstring> index, std::optional<std::wstring> name) {
    std::wstring error;
    if (!config_persistence::ApplyIniMutationsAtomically(g_sConfigPath,
            {{L"user", L"theme", std::move(index)}, {L"user", L"theme_name", std::move(name)}}, &error))
      throw std::runtime_error("Cannot prepare the private theme preference");
  };
  auto renderSaved = [] {
    LoadThemePreference();
    ThemeCheck(ApplyTheme(g_iThemeMode, false, false), "Loaded preference renders without a save");
  };
  HWND dialog = nullptr;
  const auto selected = [&](int mode) {
    return SendDlgItemMessageW(dialog, IDC_THEME_COMBO, CB_GETCURSEL, 0, 0) ==
        ThemeComboIndexFromMode(mode);
  };
  const auto apply = [&] { SendMessageW(dialog, WM_COMMAND, IDC_THEME_APPLY, 0); };
  const auto cancel = [&] { SendMessageW(dialog, WM_COMMAND, IDC_THEME_CANCEL, 0); };
  const auto tick = [&] { SendMessageW(dialog, WM_TIMER, THEME_PREVIEW_TIMER_ID, 0); };
  const auto persisted = [](int mode) {
    wchar_t value[256]{};
    GetPrivateProfileStringW(L"user", L"theme_name", L"", value, 256, g_sConfigPath.c_str());
    return ThemeDiskMode() == mode && GetThemePreferenceName(mode) == value;
  };
  const char *name = nullptr;
  switch (round) {
  case 1: {
    name = "malformed-legacy-preference-does-not-select-auto";
    themeModeledLight = 0;
    for (const wchar_t *bad : {L"not-a-number", L"2stale", L"4294967296", L"+0oops"}) {
      set(std::wstring(bad), L"Retired custom palette");
      const auto before = ThemeBytes();
      renderSaved();
      const bool light = g_iThemeMode == 1 && ThemeRoundMatches(1);
      std::cout << "THEME_MALFORMED_INDEX mode=" << g_iThemeMode << " light=" << light << '\n';
      ThemeCheck(light && ThemeBytes() == before,
                 "A malformed legacy theme index renders Light without rewriting settings");
      dialog = ThemeOpen();
      ThemeCheck(selected(1), "The dialog selects the same Light fallback it displays");
      cancel();
    }
    break;
  }
  case 2: {
    name = "stable-custom-name-wins-over-different-valid-index";
    set(L"2", GetThemePreferenceName(favorite));
    const auto before = ThemeBytes();
    renderSaved();
    dialog = ThemeOpen();
    ThemeCheck(g_iThemeMode == favorite && ThemeRoundMatches(favorite) && selected(favorite) &&
                   ThemeBytes() == before, "The stored name wins without rewriting the legacy index");
    apply();
    ThemeCheck(persisted(favorite), "Explicit Apply reconciles the name and legacy index");
    break;
  }
  case 3: {
    name = "named-system-light-recovers-an-out-of-range-index";
    set(L"90000", L"sYsTeM (lIgHt)");
    const auto before = ThemeBytes();
    renderSaved();
    dialog = ThemeOpen();
    ThemeCheck(g_iThemeMode == 1 && selected(1) && ThemeBytes() == before,
               "A case-insensitive system name wins over an obsolete index without saving");
    ThemeSelect(dialog, favorite);
    SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    ThemeCheck(ThemeRoundMatches(1) && ThemeBytes() == before,
               "The Escape command restores Light and preserves the original settings");
    break;
  }
  case 4: {
    name = "removed-theme-name-falls-back-to-valid-custom-index";
    set(std::to_wstring(favorite), L"Retired custom palette");
    const auto before = ThemeBytes();
    renderSaved();
    dialog = ThemeOpen();
    ThemeCheck(selected(favorite) && ThemeRoundMatches(favorite) && ThemeBytes() == before,
               "An unknown name falls back to a valid legacy custom index without saving");
    cancel();
    ThemeCheck(ThemeBytes() == before, "Cancel keeps the legacy preference exactly as read");
    break;
  }
  case 5: {
    name = "missing-theme-keys-default-without-dropping-browser-setting";
    set(std::nullopt, std::nullopt);
    std::wstring error;
    ThemeCheck(config_persistence::ApplyIniMutationsAtomically(g_sConfigPath,
                   {{L"user", L"browser", L"firefox"}}, &error), "Seed an unrelated private browser key");
    const auto before = ThemeBytes();
    renderSaved();
    dialog = ThemeOpen();
    const int gothic = FindThemeModeByName(DEFAULT_THEME_NAME);
    ThemeCheck(gothic >= 3 && selected(gothic) && ThemeRoundMatches(gothic) && ThemeBytes() == before,
               "Missing keys show the fresh default and preserve the unrelated file bytes");
    cancel();
    break;
  }
  case 6: {
    name = "saved-auto-reopens-with-new-system-color-setting";
    themeModeledLight = 1;
    ThemeCheck(ApplyTheme(0, false) && persisted(0) && !g_bThemeIsDark, "Auto initially saves as Light");
    const auto before = ThemeBytes();
    dialog = ThemeOpen();
    ThemeCheck(selected(0), "Auto appears as the selected mode when rendered Light");
    cancel();
    themeModeledLight = 0;
    renderSaved();
    dialog = ThemeOpen();
    ThemeCheck(selected(0) && g_bThemeIsDark && ThemeRoundMatches(0) && ThemeBytes() == before,
               "A changed system preference renders Dark without rewriting Auto");
    cancel();
    break;
  }
  case 7: {
    name = "dpi-layout-retains-pending-theme-selection";
    const auto before = ThemeBytes();
    dialog = ThemeOpen();
    ThemeSelect(dialog, favorite, false);
    RECT target{10, 20, 550, 230};
    SendMessageW(dialog, WM_DPICHANGED, MAKEWPARAM(144, 144), reinterpret_cast<LPARAM>(&target));
    RECT actual{};
    GetWindowRect(dialog, &actual);
    ThemeCheck(actual.right - actual.left == 540 && actual.bottom - actual.top == 210 && selected(favorite),
               "A hidden DPI relayout resizes the dialog without losing its pending selection");
    tick();
    ThemeCheck(ThemeRoundMatches(favorite) && ThemeBytes() == before,
               "The pending choice still previews after relayout without saving");
    cancel();
    ThemeCheck(ThemeRoundMatches(initial), "Cancel restores the saved custom palette after relayout");
    break;
  }
  case 8: {
    name = "auto-apply-resolves-system-color-again-after-preview";
    themeModeledLight = 1;
    dialog = ThemeOpen();
    ThemeSelect(dialog, 0);
    ThemeCheck(!g_bThemeIsDark && g_iThemeMode == initial, "Light Auto previews without changing the saved mode");
    themeModeledLight = 0;
    apply();
    ThemeCheck(persisted(0) && g_bThemeIsDark && ThemeRoundMatches(0),
               "Apply saves Auto and uses the system color setting at commit time");
    break;
  }
  case 9: {
    name = "explicit-legacy-zero-keeps-auto-without-a-name";
    set(L"0", std::nullopt);
    themeModeledLight = 1;
    const auto before = ThemeBytes();
    renderSaved();
    dialog = ThemeOpen();
    ThemeCheck(g_iThemeMode == 0 && selected(0) && ThemeRoundMatches(0) && ThemeBytes() == before,
               "A genuine legacy zero stays Auto even without a stable name");
    ThemeSelect(dialog, 2);
    cancel();
    ThemeCheck(!g_bThemeIsDark && ThemeBytes() == before, "Cancel returns to the legacy Auto palette");
    break;
  }
  case 10: {
    name = "auto-preview-refused-save-can-switch-to-different-choice";
    dialog = ThemeOpen();
    ThemeSelect(dialog, 0);
    const auto before = ThemeBytes();
    const auto notices = themeNotices;
    HANDLE lock = ::CreateFileW(g_sConfigPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, 0, nullptr);
    if (lock == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot lock the private theme settings");
    apply();
    CloseHandle(lock);
    ThemeCheck(IsWindow(dialog) && ThemeBytes() == before && themeNotices == notices + 1,
               "The refused Auto save does not close or overwrite the dialog's saved settings");
    ThemeSelect(dialog, favorite, false);
    apply();
    ThemeCheck(persisted(favorite) && ThemeRoundMatches(favorite) && themeNotices == notices + 1,
               "A different choice can be committed after the refused Auto save");
    break;
  }
  default:
    throw std::runtime_error("Unexpected theme round");
  }
  ThemeCheck(!IsWindow(dialog), "The private theme dialog was closed");
  ThemeAudit();
  std::cout << "APP_THEME_ROUND round=" << round << " case=" << name << '\n';
}
