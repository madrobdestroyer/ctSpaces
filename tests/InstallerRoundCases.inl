void RunInstallerRound(int round) {
  Fixture fixture(L"round-case");
  fixture.SeedInstalled("private installer comparison");
  const auto prior = ReadBytes(fixture.installedExe());
  const char *name = nullptr;
  switch (round) {
  case 1:
    name = "revision-only-update-decline-and-exit";
    SetVersions(L"6.3.5.2", L"6.3.5.1");
    break;
  case 2:
    name = "numeric-version-patch-9-to-10";
    SetVersions(L"6.3.10.0", L"6.3.9.0");
    break;
  case 3:
    name = "numeric-version-major-9-to-10";
    SetVersions(L"10.0.0.0", L"9.9.9.9");
    break;
  case 4:
    name = "leading-zero-components-compare-equal";
    SetVersions(L"6.03.005.0", L"6.3.5.0");
    break;
  case 5:
    name = "older-revision-keeps-newer-copy";
    SetVersions(L"6.3.5.8", L"6.3.5.9");
    break;
  case 6:
    name = "older-major-keeps-newer-copy";
    SetVersions(L"9.9.9.9", L"10.0.0.0");
    break;
  case 7:
    name = "older-minor-keeps-newer-copy";
    SetVersions(L"6.9.9.9", L"6.10.0.0");
    break;
  case 8:
    name = "corrected-version-update-decline";
    SetVersions(L"5.0.0.4", L"5.0.1.0");
    break;
  case 9:
    name = "corrected-version-offer-excludes-other-installed-builds";
    SetVersions(L"5.0.0.4", L"5.0.2.0");
    break;
  case 10:
    name = "unreadable-installed-version-run-once-preserves-data";
    SetVersions(L"6.3.5.0", L"");
    break;
  default:
    Fail("Unexpected private installer round");
  }

  const auto current = g_currentVersion;
  const auto installed = g_installedVersion;
  const bool older = round == 5 || round == 6 || round == 7 || round == 9;
  const bool runOnce = round != 1 && round != 7;
  if (older)
    ResetBoundaries({runOnce ? IDYES : IDNO});
  else
    ResetBoundaries({IDNO, runOnce ? IDYES : IDNO});
  SetVersions(current, installed);
  Check(chkUpdate() == runOnce, "Installer follows the explicit Run Once decision");
  Check(g_nextAnswer == (older ? 1u : 2u), "Version choice consumes the expected prompts");
  if (round == 4)
    Check(HasMessage(L"Replace Installed Copy", L"both report version"),
          "Numerically equal components use the equal-version prompt");
  else if (round == 8)
    Check(HasMessage(L"Update ctSpaces", L"corrected 5.0.0.x"),
          "The corrected version offers an update through the normal prompt");
  else if (round == 10)
    Check(HasMessage(L"Replace Installed Copy", L"version could not be read"),
          "The unknown installed version is reported explicitly");
  else if (older)
    Check(HasMessage(L"Run Once", L"installed copy is newer"),
          "An older candidate gets the correct comparison prompt");
  else
    Check(HasMessage(L"Update ctSpaces", L"copy is newer"),
          "A newer candidate gets the correct comparison prompt");
  Check(g_launchCalls == 0 && g_registryReads == 0 && g_knownFolderCalls == 0 &&
            ReadBytes(fixture.installedExe()) == prior,
        "The declined replacement preserves the exact installed bytes and requests no launch");
  fixture.CheckSentinelsAndNoStageLitter();
  std::cout << "APP_INSTALLER_ROUND round=" << round << " case=" << name << '\n';
}
