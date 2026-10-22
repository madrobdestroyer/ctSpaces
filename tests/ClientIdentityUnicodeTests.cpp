// Reuse the production-linked disposable fixture and its Win32 test boundaries.
#define wmain ClientRenameUnicodeFixtureEntry
#include "ClientRenameUnicodeTests.cpp"
#undef wmain

int wmain() {
  const fs::path fixture = fs::current_path() / L"build" /
      (L"ci-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
       std::to_wstring(GetTickCount64()));
  try {
    Check(std::setlocale(LC_CTYPE, "C") != nullptr, "C locale enforced");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_hInst = GetModuleHandleW(nullptr); g_bQaInstance = true;
    g_sExeDir = GetCurrentExecutablePath().parent_path();
    g_hGui = CreateWindowExW(0, L"STATIC", L"Unicode identity tests", 0,
        0, 0, 1, 1, HWND_MESSAGE, nullptr, g_hInst, nullptr);
    unsigned scenario = 0;
    for (const auto &names : std::vector<std::pair<std::wstring, std::wstring>>{
         {L"M\u00FCnchen", L"M\u00DCNCHEN"},
         {L"\u00E9quipe", L"\u00C9QUIPE"},
         {L"\u03B1\u03B8\u03AE\u03BD\u03B1", L"\u0391\u0398\u0389\u039D\u0391"},
         {L"\u043E\u0444\u0438\u0441", L"\u041E\u0424\u0418\u0421"}}) {
      const std::wstring caseName = L"case" + std::to_wstring(scenario++);
      Setup(fixture, caseName.c_str(), names.first);
      Check(ResolveExistingClientName(names.second) == names.first,
          "Typed Unicode variant resolves to exact existing directory spelling");
      Check(IsClientPinned(names.second), "Pinned identity accepts accented case variant");
      Check(!ShouldRestoreTabsForClient(names.second, BrowserKind::Edge) &&
            !ShouldRestoreTabsForClient(names.second, BrowserKind::Chrome),
          "Both disabled restore preferences survive accented case lookup");
      Check(ShouldRestoreTabsForClient(names.second, BrowserKind::Brave),
          "A different browser preference stays independent");
      g_archivedClients = {names.first};
      Check(IsClientArchived(names.second), "Archive membership accepts case variant");

      std::map<std::wstring, unsigned, CaseInsensitiveLess> clientMap;
      clientMap.emplace(names.first, 11);
      Check(clientMap.find(names.second) != clientMap.end(),
          "Client map ordering has the same ordinal equality as Windows directories");
      Check(!clientMap.emplace(names.second, 22).second && clientMap.size() == 1,
          "Client map cannot duplicate a case-equivalent name");
      g_activeProfiles.clear();
      for (const auto browser : {BrowserKind::Edge, BrowserKind::Chrome,
                                BrowserKind::Brave, BrowserKind::Firefox}) {
        ActiveProfileInfo active; active.pid = 100 + static_cast<DWORD>(browser);
        g_activeProfiles.emplace(ClientBrowserKey{names.first, browser}, active);
        const auto found = GetActiveProfile(names.second, browser);
        Check(found && found->pid == active.pid,
            "Active profile lookup accepts Unicode variant and preserves browser identity");
        Check(!g_activeProfiles.emplace(ClientBrowserKey{names.second, browser},
                                       ActiveProfileInfo{}).second,
            "Active map cannot duplicate one browser under accented casing");
      }
      Check(g_activeProfiles.size() == 4, "Four browser slots remain independent");
      Check(IsClientActiveAnyBrowser(names.second),
          "Tracked-client UI recognizes Unicode case variant");
      std::wstring error;
      Check(ProbeClientProfilesInUse(names.second, error) == ProfileUseState::InUse &&
            error.find(L"tracking an open browser") != std::wstring::npos,
          "Active-client safety preflight catches typed accented case before OS process inspection");
      g_activeProfiles.clear();

      Check(RestoreArchivedClient(names.second, &error) && g_archivedClients.empty(),
          "Archive restore removes the stored original spelling via typed case variant");
      Check(Sentinels(names.first), "Archive restore preserves notes and both profile sentinels");
      std::vector<std::wstring> identities{
          names.second, names.second + L"|edge", names.second + L"|chrome", L"Unrelated"};
      Check(ReplaceClientName(identities, names.first, L"Renamed"),
          "Rename identity migration recognizes equivalent accented spelling");
      Check(identities == std::vector<std::wstring>{
          L"Renamed", L"Renamed|edge", L"Renamed|chrome", L"Unrelated"},
          "Migration preserves qualified browser keys and unrelated clients");
    }
    ActiveProfileMap distinct;
    for (const auto &name : {L"Stra\u00DFe", L"STRASSE", L"Caf\u00E9", L"Cafe\u0301"})
      distinct.emplace(ClientBrowserKey{name, BrowserKind::Edge}, ActiveProfileInfo{});
    Check(distinct.size() == 4, "Sharp-s and combining forms remain distinct client identities");
    Check(client_name_comparison::CompareIgnoringCase(L"", L"") == 0 &&
          client_name_comparison::CompareIgnoringCase(L"", L"a") < 0 &&
          client_name_comparison::CompareIgnoringCase(L"a", L"") > 0,
          "Empty comparator values preserve strict ordering");
    DestroyWindow(g_hGui); g_hGui = nullptr; CoUninitialize();
    fs::remove_all(fixture);
    std::cout << checks << " production Unicode client identity checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL after " << checks << " checks: " << error.what()
              << "\nFixture retained: " << fixture.string() << '\n';
    g_activeProfiles.clear();
    if (g_hGui) DestroyWindow(g_hGui);
    return 1;
  }
}
