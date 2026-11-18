static bool ThemeRoundMatches(int mode) {
  if (mode >= 3) return ThemeMatches(mode);
  ThemeColors expected{};
  const bool dark = mode == 2 || (mode == 0 && IsSystemDarkMode());
  BuildThemeColors(dark, expected);
  return g_bThemeIsDark == dark && g_themeColors.crWindow == expected.crWindow &&
         g_themeColors.crControl == expected.crControl &&
         g_themeColors.crAccent == expected.crAccent;
}

static void ThemeRoundChooseRow(HWND dialog, int row) {
  const HWND combo = GetDlgItem(dialog, IDC_THEME_COMBO);
  SendMessageW(combo, CB_SETCURSEL, row, 0);
  SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDC_THEME_COMBO, CBN_SELCHANGE),
               reinterpret_cast<LPARAM>(combo));
}

static void RunThemeRound(const fs::path &root, int round) {
  constexpr int initial = 3, preview = 4, other = 5;
  ThemeSeed(root, initial);
  const auto saved = ThemeBytes();
  HWND dialog = ThemeOpen();
  const auto apply = [&] { SendMessageW(dialog, WM_COMMAND, IDC_THEME_APPLY, 0); };
  const auto cancel = [&] { SendMessageW(dialog, WM_COMMAND, IDC_THEME_CANCEL, 0); };
  const auto tick = [&] { SendMessageW(dialog, WM_TIMER, THEME_PREVIEW_TIMER_ID, 0); };
  const auto persisted = [&](int mode) {
    wchar_t name[256]{};
    GetPrivateProfileStringW(L"user", L"theme_name", L"", name, 256, g_sConfigPath.c_str());
    return ThemeDiskMode() == mode && GetThemePreferenceName(mode) == name;
  };
  const char *name = nullptr;
  switch (round) {
  case 1:
    name = "separator-preserves-pending-custom-choice";
    ThemeSelect(dialog, preview, false);
    ThemeRoundChooseRow(dialog, g_iThemeSeparatorIndex);
    ThemeCheck(SendDlgItemMessageW(dialog, IDC_THEME_COMBO, CB_GETCURSEL, 0, 0) ==
                   ThemeComboIndexFromMode(preview), "Separator restores the prior selectable row");
    tick();
    ThemeCheck(ThemeRoundMatches(preview) && ThemeBytes() == saved,
               "Pending custom choice still previews with the saved file intact");
    cancel();
    break;
  case 2:
    name = "multiple-previews-apply-final-choice";
    ThemeSelect(dialog, preview);
    ThemeSelect(dialog, other, false);
    ThemeCheck(ThemeRoundMatches(preview) && ThemeBytes() == saved,
               "The earlier preview remains until the next selection is applied");
    apply();
    ThemeCheck(!IsWindow(dialog) && ThemeRoundMatches(other) && persisted(other),
               "Apply commits the last choice and its name before the next timer");
    break;
  case 3:
    name = "custom-to-system-light-cancel";
    ThemeSelect(dialog, 1);
    ThemeCheck(ThemeRoundMatches(1) && ThemeBytes() == saved,
               "System Light previews its palette without writing");
    cancel();
    ThemeCheck(!IsWindow(dialog) && ThemeRoundMatches(initial) && persisted(initial),
               "Cancel restores the saved custom palette after System Light");
    break;
  case 4:
    name = "custom-to-system-dark-apply-and-reopen";
    ThemeSelect(dialog, 2, false);
    apply();
    ThemeCheck(!IsWindow(dialog) && ThemeRoundMatches(2) && persisted(2),
               "System Dark saves its matching palette");
    dialog = ThemeOpen();
    ThemeCheck(SendDlgItemMessageW(dialog, IDC_THEME_COMBO, CB_GETCURSEL, 0, 0) ==
                   ThemeComboIndexFromMode(2), "The next dialog selects saved System Dark");
    cancel();
    break;
  case 5:
    name = "custom-to-system-auto-apply-and-reload";
    ThemeSelect(dialog, 0);
    apply();
    ThemeCheck(!IsWindow(dialog) && persisted(0) && ThemeRoundMatches(0),
               "Apply persists Auto and renders the system-selected palette");
    LoadThemePreference();
    ThemeCheck(g_iThemeMode == 0, "The preference loader keeps the explicit Auto choice");
    break;
  case 6:
    name = "return-to-saved-choice-before-preview";
    ThemeSelect(dialog, preview, false);
    ThemeSelect(dialog, initial, false);
    tick();
    ThemeCheck(ThemeRoundMatches(initial) && ThemeBytes() == saved,
               "A stale preview timer cannot restore the abandoned choice");
    cancel();
    ThemeCheck(!IsWindow(dialog) && persisted(initial), "Cancel leaves the saved choice intact");
    break;
  case 7:
    name = "invalid-combo-selection-retains-preview";
    ThemeSelect(dialog, preview);
    ThemeRoundChooseRow(dialog, -1);
    tick();
    ThemeCheck(ThemeRoundMatches(preview) && ThemeBytes() == saved,
               "A cleared native combo selection leaves the last preview intact");
    apply();
    ThemeCheck(!IsWindow(dialog) && persisted(preview),
               "Apply uses the last valid choice after a cleared selection");
    break;
  case 8:
    name = "pending-preview-close-before-next-open";
    ThemeSelect(dialog, preview, false);
    SendMessageW(dialog, WM_CLOSE, 0, 0);
    ThemeCheck(!IsWindow(dialog) && ThemeRoundMatches(initial) && ThemeBytes() == saved,
               "Closing before the preview timer preserves the saved palette and bytes");
    dialog = ThemeOpen();
    tick();
    ThemeCheck(ThemeRoundMatches(initial), "A timer on the new dialog cannot use the old selection");
    cancel();
    break;
  case 9: {
    name = "system-preview-locked-apply-retry";
    ThemeSelect(dialog, 2);
    const auto notices = themeNotices;
    HANDLE lock = ::CreateFileW(g_sConfigPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, 0, nullptr);
    if (lock == INVALID_HANDLE_VALUE) throw std::runtime_error("Private theme lock failed");
    apply();
    CloseHandle(lock);
    ThemeCheck(IsWindow(dialog) && ThemeRoundMatches(2) && ThemeBytes() == saved &&
                   themeNotices == notices + 1, "A locked System Dark apply retains the preview");
    apply();
    ThemeCheck(!IsWindow(dialog) && persisted(2), "Retry commits System Dark once unlocked");
    break;
  }
  case 10: {
    name = "custom-apply-preserves-unrelated-settings";
    ThemeSelect(dialog, other);
    std::wstring detail;
    ThemeCheck(config_persistence::ApplyIniMutationsAtomically(
                   g_sConfigPath, {{L"private", L"unrelated", L"keep after apply"}}, &detail),
               "An independent private preference is saved while the theme is previewing");
    apply();
    wchar_t value[64]{};
    GetPrivateProfileStringW(L"private", L"unrelated", L"", value, 64, g_sConfigPath.c_str());
    ThemeCheck(!IsWindow(dialog) && persisted(other) && std::wstring_view(value) == L"keep after apply",
               "Applying the preview preserves the unrelated preference");
    break;
  }
  default:
    throw std::runtime_error("Unknown theme round");
  }
  ThemeCheck(!IsWindow(dialog), "The private theme dialog was closed");
  ThemeAudit();
  std::cout << "APP_THEME_ROUND round=" << round << " case=" << name << '\n';
}
