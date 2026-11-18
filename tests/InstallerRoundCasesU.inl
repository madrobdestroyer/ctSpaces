void RunInstallerRoundU(int round) {
  Fixture fixture(L"round-u");
  const auto versions = [](const wchar_t *running = L"6.3.5.0", const wchar_t *installed = L"6.3.4.0") {
    SetVersions(running, installed);
  };
  const auto noSetupEffects = [&] {
    Check(g_registryReads == 0 && g_knownFolderCalls == 0,
          "An existing-copy update must not repeat the fresh setup choices");
    fixture.CheckSentinelsAndNoStageLitter();
  };
  const auto didCopyAndLaunch = [&] {
    CheckInstalledMatchesCurrent(fixture);
    Check(g_launchCalls == 1, "An accepted install requested exactly one launch");
  };
  const char *name = nullptr;
  switch (round) {
  case 1: {
    name = "fresh-setup-stage-refusal-then-retry";
    ResetBoundaries({IDYES});
    g_failStageCreation = true;
    Check(doInstall() && !fs::exists(fixture.installedExe()) && g_launchCalls == 0 &&
          HasMessage(L"Set Up ctSpaces", L"staging file could not be created"),
          "A failed fresh setup leaves no executable and explains the staging failure");
    noSetupEffects();
    ResetBoundaries({IDYES, IDNO, IDNO, IDNO});
    Check(!doInstall() && g_nextAnswer == 4, "Retry completes fresh setup and leaves via relaunch");
    didCopyAndLaunch();
    break;
  }
  case 2: {
    name = "equal-version-destination-lock-then-successful-retry";
    fixture.SeedInstalled();
    const auto before = ReadBytes(fixture.installedExe());
    HANDLE lock = ::CreateFileW(fixture.installedExe().c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(lock != INVALID_HANDLE_VALUE, "Could not lock the private replacement target");
    ResetBoundaries({IDYES});
    versions(L"6.3.5.0", L"6.3.5.0");
    const bool keepRunning = chkUpdate();
    CloseHandle(lock);
    Check(keepRunning && g_launchCalls == 0 && ReadBytes(fixture.installedExe()) == before &&
          HasMessage(L"Update ctSpaces", L"Atomic replacement failed"),
          "The locked equal-version target remains intact and the error explains why");
    noSetupEffects();
    ResetBoundaries({IDYES});
    versions(L"6.3.5.0", L"6.3.5.0");
    Check(!chkUpdate(), "A subsequent equal-version replacement succeeds after unlocking");
    didCopyAndLaunch();
    noSetupEffects();
    break;
  }
  case 3: {
    name = "fresh-relaunch-failure-is-followed-by-safe-equal-decline";
    ResetBoundaries({IDYES, IDNO, IDNO, IDNO});
    g_launchShouldSucceed = false;
    Check(doInstall() && HasMessage(L"Set Up ctSpaces", L"installed, but Windows could not start"),
          "Fresh setup keeps this copy running after the installed relaunch fails");
    didCopyAndLaunch();
    const auto committed = ReadBytes(fixture.installedExe());
    ResetBoundaries({IDNO, IDYES});
    Check(chkUpdate() && g_nextAnswer == 2 && g_launchCalls == 0 &&
          HasMessage(L"Replace Installed Copy", L"both report version") &&
          ReadBytes(fixture.installedExe()) == committed,
          "A follow-up run sees the committed version and keeps it on explicit decline");
    noSetupEffects();
    break;
  }
  case 4:
    name = "unreadable-installed-resource-can-be-explicitly-replaced";
    fixture.SeedInstalled("unversioned file bytes inside private installer fixture");
    ResetBoundaries({IDYES});
    Check(GetExeVersion(fixture.installedExe()).empty() && !chkUpdate() &&
          HasMessage(L"Replace Installed Copy", L"version could not be read"),
          "Replacement requires explicit consent when the real installed resource is unreadable");
    didCopyAndLaunch();
    Check(GetExeVersion(fixture.installedExe()) == CTSPACES_VERSION_WTEXT,
          "The replacement has the real candidate version resource");
    noSetupEffects();
    break;
  case 5:
    name = "start-menu-denial-is-optional-and-does-not-touch-desktop";
    ResetBoundaries({IDYES, IDYES, IDNO, IDNO});
    Check(!doInstall() && g_nextAnswer == 4 && g_knownFolderCalls == 1 && g_registryReads == 1 &&
          HasMessage(L"Set Up ctSpaces", L"Start Menu shortcut: Windows could not locate") &&
          !HasMessage(L"Set Up ctSpaces", L"Desktop shortcut:"),
          "Only the requested Start Menu location is resolved and its refusal does not block setup");
    didCopyAndLaunch();
    break;
  case 6:
    name = "desktop-denial-is-optional-and-does-not-touch-start-menu";
    ResetBoundaries({IDYES, IDNO, IDYES, IDNO});
    Check(!doInstall() && g_nextAnswer == 4 && g_knownFolderCalls == 1 && g_registryReads == 1 &&
          HasMessage(L"Set Up ctSpaces", L"Desktop shortcut: Windows could not locate") &&
          !HasMessage(L"Set Up ctSpaces", L"Start Menu shortcut:"),
          "Only the requested Desktop location is resolved and its refusal does not block setup");
    didCopyAndLaunch();
    break;
  case 7:
    name = "both-optional-shortcut-refusals-are-reported-together";
    ResetBoundaries({IDYES, IDYES, IDYES, IDNO});
    Check(!doInstall() && g_nextAnswer == 4 && g_knownFolderCalls == 2 && g_registryReads == 1,
          "Requested shortcut lookups are intercepted independently");
    Check(std::ranges::count_if(g_messages, [](const CapturedMessage &message) {
            return message.text.find(L"Start Menu shortcut:") != std::wstring::npos &&
                   message.text.find(L"Desktop shortcut:") != std::wstring::npos;
          }) == 1, "One non-blocking warning includes both optional shortcut errors");
    didCopyAndLaunch();
    break;
  case 8: {
    name = "equal-version-launch-refusal-preserves-new-copy-for-next-attempt";
    fixture.SeedInstalled("previous private executable with the same version");
    ResetBoundaries({IDYES});
    versions(L"6.3.5.0", L"6.3.5.0");
    g_launchShouldSucceed = false;
    Check(chkUpdate() && HasMessage(L"Replace Installed Copy", L"replaced, but Windows could not start"),
          "Equal-version launch failure names the actual successful replacement");
    didCopyAndLaunch();
    noSetupEffects();
    ResetBoundaries({IDYES});
    Check(!chkUpdate(), "The next explicit attempt can relaunch the same exact build");
    didCopyAndLaunch();
    noSetupEffects();
    break;
  }
  case 9: {
    name = "declined-fresh-install-can-later-be-accepted";
    ResetBoundaries({IDNO, IDYES});
    Check(doInstall() && !fs::exists(fixture.installedExe()) && g_launchCalls == 0 &&
          HasMessage(L"Run Once", L"without installing"), "Run Once makes no private install");
    noSetupEffects();
    ResetBoundaries({IDYES, IDNO, IDNO, IDNO});
    Check(!doInstall(), "A later accepted setup is not suppressed by the earlier Run Once");
    didCopyAndLaunch();
    break;
  }
  case 10: {
    name = "distinct-install-fixtures-remain-independent-during-update";
    fixture.SeedInstalled("independent private installation bytes");
    const auto untouched = ReadBytes(fixture.installedExe());
    {
      Fixture targeted(L"round-u-other");
      targeted.SeedInstalled("the chosen older installation");
      ResetBoundaries({IDYES});
      versions();
      Check(!chkUpdate() && g_launchCalls == 1, "The explicitly targeted fixture updates and relaunches once");
      CheckInstalledMatchesCurrent(targeted);
    }
    Check(ReadBytes(fixture.installedExe()) == untouched, "A separate installation and its executable remain untouched");
    noSetupEffects();
    break;
  }
  default:
    Fail("Unexpected private installer round");
  }
  std::cout << "APP_INSTALLER_ROUND round=" << round << " case=" << name << '\n';
}
