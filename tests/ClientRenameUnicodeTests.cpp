#define CTSPACES_INSTALLER_TEST_HOOKS
#define CTSPACES_RESTORE_GUI_TEST_HOOKS
#define CTSPACES_CLIENT_NAMESPACE_TEST_HOOKS
#define wWinMain CtSpacesProductionWinMain
#include "../ctSpaces.cpp"
#undef wWinMain
#include <clocale>
#include "DirectoryJunctionFixture.h"

static bool restoreGuiEnabled = false;
static unsigned restoreGuiMode = 0;
static fs::path restoreGuiArchive, restoreGuiStage, restoreGuiOutside;
static std::wstring restoreGuiMessage;
static bool restoreGuiStageCalled = false, restoreGuiGuardCalled = false;
static bool unicodeNamespaceAttack = false, unicodeNamespaceObserved = false;
static unsigned unicodeNamespaceMoves = 0;
static DWORD unicodeNamespaceMutationError = 0;
static BY_HANDLE_FILE_INFORMATION unicodeNamespaceOriginal{};

namespace client_deletion_safety {
void BeforeNamespaceUpgrade(const fs::path &) {}
void BeforeNamespaceMove(const fs::path &source, const fs::path &, HANDLE original) {
  if (!unicodeNamespaceAttack) return;
  ++unicodeNamespaceMoves;
  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(original, &info)) throw std::runtime_error("Unicode bound source identity unavailable");
  if (unicodeNamespaceMoves == 1) { unicodeNamespaceOriginal = info; return; }
  if (unicodeNamespaceMoves != 2) return;
  if (info.dwVolumeSerialNumber != unicodeNamespaceOriginal.dwVolumeSerialNumber ||
      info.nFileIndexHigh != unicodeNamespaceOriginal.nFileIndexHigh || info.nFileIndexLow != unicodeNamespaceOriginal.nFileIndexLow)
    throw std::runtime_error("Unicode second hop changed its original bound source");
  unicodeNamespaceAttack = false; unicodeNamespaceObserved = true;
  if (MoveFileExW(source.c_str(), (g_sDataDir / L"displaced-unicode-stage").c_str(), 0))
    throw std::runtime_error("Unicode case-only intermediate was replaceable during commit");
  unicodeNamespaceMutationError = GetLastError();
}
}

int CtSpacesInstallerTestMessageBox(HWND, LPCWSTR text, LPCWSTR title, UINT flags) {
  if (restoreGuiEnabled) {
    restoreGuiMessage = text;
    return (flags & MB_YESNO) ? IDYES : IDOK;
  }
  std::wcerr << L"TEST NOTICE " << title << L": " << text << L'\n';
  return (flags & MB_YESNO) ? IDNO : IDOK;
}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path &, std::wstring &) { return false; }
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path &) {}
BOOL CtSpacesRestoreGuiTestPickArchive(OPENFILENAMEW *picker) {
  return wcscpy_s(picker->lpstrFile, picker->nMaxFile, restoreGuiArchive.c_str()) == 0;
}
std::wstring CtSpacesRestoreGuiTestStageSuffix(unsigned attempt) {
  return attempt == 0 || restoreGuiMode == 7 ? L"collision" : L"consumer";
}
CtBackup::Result CtSpacesRestoreGuiTestRunStage(
    const std::function<CtBackup::Result(_7zUiCtx &)> &operation) {
  restoreGuiStageCalled = true;
  _7zUiCtx context;
  auto result = operation(context); // Real extractor/normalizer; only progress UI is skipped.
  restoreGuiStage = g_sDataDir / L"Restore_Temp_consumer";
  if (result.ok() && restoreGuiMode == 6) return {E_FAIL, L"Focused staging cancellation after extraction."};
  return result;
}
void CtSpacesRestoreGuiTestAfterStage(CtBackup::StagedRestore &staged) {
  restoreGuiStage = staged.stagingRoot;
  if (restoreGuiMode == 2) {
    HANDLE probe = CreateFileW(staged.payloadDir.c_str(), DELETE | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    const DWORD openError = probe == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    BY_HANDLE_FILE_INFORMATION expectedIdentity{};
    const bool captured = probe != INVALID_HANDLE_VALUE && GetFileInformationByHandle(probe, &expectedIdentity) &&
        (expectedIdentity.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
        !(expectedIdentity.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && GetFileType(probe) == FILE_TYPE_DISK;
    if (probe != INVALID_HANDLE_VALUE) CloseHandle(probe);
    const fs::path original = g_sDataDir / L"original-staged-payload";
    if (!captured) throw std::runtime_error("GUI payload fixture could not capture its original directory identity");
    const auto sameDirectory = [&](const fs::path &path) {
      HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
          FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
      if (handle == INVALID_HANDLE_VALUE) return false;
      BY_HANDLE_FILE_INFORMATION current{};
      const bool same = GetFileInformationByHandle(handle, &current) &&
          (current.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
          !(current.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && GetFileType(handle) == FILE_TYPE_DISK &&
          current.dwVolumeSerialNumber == expectedIdentity.dwVolumeSerialNumber &&
          current.nFileIndexHigh == expectedIdentity.nFileIndexHigh && current.nFileIndexLow == expectedIdentity.nFileIndexLow;
      CloseHandle(handle);
      return same;
    };
    const auto absent = [](const fs::path &path) {
      if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return false;
      const DWORD error = GetLastError();
      return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    };
    DWORD moveError = ERROR_SUCCESS;
    unsigned attempts = 0, transientDenials = 0;
    bool moved = false;
    for (; attempts < 8;) {
      ++attempts;
      // Retry only this disposable attack setup. Never move a substituted
      // source or replace an occupied destination to make the test pass.
      if (!sameDirectory(staged.payloadDir) || !absent(original))
        throw std::runtime_error("GUI payload fixture identity or destination changed before its move");
      if (MoveFileExW(staged.payloadDir.c_str(), original.c_str(), 0)) {
        if (!sameDirectory(original) || !absent(staged.payloadDir))
          throw std::runtime_error("GUI payload fixture move did not preserve its exact directory identity");
        moved = true;
        break;
      }
      moveError = GetLastError();
      if ((moveError != ERROR_ACCESS_DENIED && moveError != ERROR_SHARING_VIOLATION &&
           moveError != ERROR_LOCK_VIOLATION) || attempts == 8) break;
      ++transientDenials;
      Sleep(1u << (attempts - 1));
    }
    std::wcerr << L"GUI fixture payload move attempts=" << attempts
               << L", transientDenials=" << transientDenials << L", moved=" << moved << L'\n';
    if (!moved) {
      std::wcerr << L"GUI fixture mutation phase afterStage, guardCalled=" << restoreGuiGuardCalled
                 << L", deleteOpenError=" << openError << L", moveError=" << moveError
                 << L", attempts=" << attempts
                 << L", source=" << staged.payloadDir << L", destination=" << original << L'\n';
      throw std::runtime_error("GUI payload fixture move failed before Commit");
    }
    if (!CtTest::CreateDirectoryJunction(staged.payloadDir, restoreGuiOutside))
      throw std::runtime_error("GUI junction fixture failed");
  }
  if (restoreGuiMode == 3) g_activeProfiles.emplace(ClientBrowserKey{L"Keep", BrowserKind::Edge}, ActiveProfileInfo{});
}
void CtSpacesRestoreGuiTestBeforeCommitGuard() {
  restoreGuiGuardCalled = true;
  if (restoreGuiMode == 4) g_activeProfiles.emplace(ClientBrowserKey{L"Keep", BrowserKind::Edge}, ActiveProfileInfo{});
}

namespace {
unsigned checks = 0;
void Check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
void Write(const fs::path &path, const std::string &value) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << value;
  if (!output) throw std::runtime_error("fixture write failed");
}
std::string Read(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), {}};
}
void SeedClient(const std::wstring &name) {
  const fs::path root = g_sDataDir / L"Sites" / name;
  Write(root / L"ctSpaces-client-v2", "ctSpaces-client-schema=2\r\n");
  Write(root / L"ctSpaces-client-notes.txt", "Unicode notebook sentinel\r\n");
  for (auto browser : {BrowserKind::Edge, BrowserKind::Chrome}) {
    const fs::path slot = root / L"Browsers" / GetBrowserId(browser);
    Write(slot / L"ctSpaces-browser-v2", std::string("ctSpaces-browser-schema=2\r\nbrowser=") + (browser == BrowserKind::Edge ? "edge" : "chrome") + "\r\n");
    Write(slot / L"Profile" / L"ctSpaces", "ctSpaces-profile=2\r\n");
    Write(slot / L"Profile" / L"Default" / L"Preferences", "{}");
    Write(slot / L"Profile" / L"sentinel", "Unicode private profile");
  }
}
bool Sentinels(const std::wstring &name) {
  const fs::path root = g_sDataDir / L"Sites" / name;
  return Read(root / L"ctSpaces-client-notes.txt") == "Unicode notebook sentinel\r\n" &&
    Read(root / L"Browsers" / L"edge" / L"Profile" / L"sentinel") == "Unicode private profile" &&
    Read(root / L"Browsers" / L"chrome" / L"Profile" / L"sentinel") == "Unicode private profile";
}
void Setup(const fs::path &fixture, const wchar_t *caseName, const std::wstring &name) {
  g_sDataDir = fixture / caseName / L"d";
  fs::create_directories(g_sDataDir);
  g_sConfigPath = g_sDataDir / L"config.ini";
  g_sShortcutDesktopOverride = fixture / caseName / L"s";
  fs::create_directories(g_sShortcutDesktopOverride);
  g_pinnedClients = {name};
  g_archivedClients.clear();
  g_clientsWithoutTabRestore = {name + L"|edge", name + L"|chrome"};
  g_allClientNames.clear();
  SeedClient(name);
  Check(SavePinnedClients(), "Unicode fixture pinned settings saved");
  Check(SaveArchivedClients(), "Unicode fixture archive settings saved");
  Check(SaveRestoreTabsPreferences(), "Unicode fixture restore settings saved");
}
void LegacyRestoreIdentity(const fs::path &fixture) {
  const fs::path base = fixture / L"legacy";
  const fs::path stage = base / L"stage";
  Write(stage / L"Customer" / L"Default" / L"state.bin", "RESTORE");
  CtBackup::StagedRestore staged;
  Check(NormalizeLegacyZipStaging(stage, staged).ok() && staged.validation,
        "Actual legacy ZIP normalizer seals its normalized client tree");
  const fs::path live = base / L"live" / L"Sites";
  Write(live / L"Keep" / L"state.bin", "ORIGINAL");
  Check(CtBackup::CommitStagedRestore(staged, live, base / L"recovery").ok(),
        "Actual normalized legacy ZIP stage commits through identity protection");
  Check(Read(live / L"Customer" / L"Default" / L"state.bin") == "RESTORE" &&
        Read(base / L"recovery" / L"Keep" / L"state.bin") == "ORIGINAL" && !fs::exists(stage),
        "Legacy identity commit preserves recovery and cleans only its own stage");
  const fs::path outside = base / L"outside";
  Write(outside / L"Sites" / L"Customer" / L"Default" / L"state.bin", "OUTSIDE");
  const fs::path rootLink = base / L"root-link";
  Check(CtTest::CreateDirectoryJunction(rootLink, outside), "Legacy staging root junction fixture created");
  Check(!NormalizeLegacyZipStaging(rootLink, staged).ok() && staged.stagingRoot.empty(),
        "Actual legacy normalizer rejects a root junction before traversing it");
  Check(RemoveDirectoryW(rootLink.c_str()) != FALSE, "Legacy root cleanup deletes only link object");
  const fs::path payloadStage = base / L"payload-stage";
  fs::create_directory(payloadStage);
  Check(CtTest::CreateDirectoryJunction(payloadStage / L"Sites", outside / L"Sites"),
        "Legacy payload root junction fixture created");
  Check(!NormalizeLegacyZipStaging(payloadStage, staged).ok() && staged.stagingRoot.empty(),
        "Actual legacy normalizer rejects a payload junction before traversing it");
  Check(Read(outside / L"Sites" / L"Customer" / L"Default" / L"state.bin") == "OUTSIDE",
        "Rejected legacy roots preserve outside fixture bytes");
  Check(RemoveDirectoryW((payloadStage / L"Sites").c_str()) != FALSE,
        "Legacy payload cleanup deletes only the link object");
}
void GuiRestoreConsumerSafety(const fs::path &fixture) {
  for (const unsigned mode : {1u, 2u, 3u, 4u, 6u, 7u}) {
    const fs::path base = fixture / L"gui-restore" / std::to_wstring(mode);
    g_sDataDir = base / L"data";
    g_sConfigPath = g_sDataDir / L"config.ini";
    g_allClientNames.clear();g_pinnedClients.clear();g_archivedClients.clear();g_activeProfiles.clear();
    g_clientsWithoutTabRestore.clear();
    const fs::path source = base / L"source" / L"Sites";
    Write(source / L"Alpha" / L"Default" / L"state.bin", "BACKUP");
    Write(source / L"Alpha" / L"Default" / L"ctSpaces", "ctSpaces-profile=1");
    Write(g_sDataDir / L"Sites" / L"Keep" / L"Default" / L"state.bin", "ORIGINAL");
    Write(g_sDataDir / L"Sites" / L"Keep" / L"Default" / L"ctSpaces", "ctSpaces-profile=1");
    const fs::path collision = g_sDataDir / L"Restore_Temp_collision";
    Write(collision / L"unrelated-sentinel", "PREEXISTING COLLISION");
    restoreGuiOutside = base / L"outside" / L"Sites";
    Write(restoreGuiOutside / L"Alpha" / L"Default" / L"state.bin", "OUTSIDE");
    restoreGuiArchive = base / L"backup.7z";
    Check(CtBackup::CreateValidated7zBackup(source, restoreGuiArchive, CTSPACES_VERSION_TEXT, nullptr, nullptr).ok(),
          "Actual restore GUI fixture creates a verified archive");
    restoreGuiMode = mode;restoreGuiStage.clear();restoreGuiMessage.clear();
    restoreGuiStageCalled = restoreGuiGuardCalled = false;restoreGuiEnabled = true;
    GuiProfRestoreAll();restoreGuiEnabled = false;g_activeProfiles.clear();
    Check(Read(collision / L"unrelated-sentinel") == "PREEXISTING COLLISION",
          "Actual restore GUI never deletes a preexisting staging-name collision");
    Check(Read(restoreGuiOutside / L"Alpha" / L"Default" / L"state.bin") == "OUTSIDE",
          "Actual restore GUI leaves outside junction target bytes unchanged");
    Check(g_bUiEnabled, "Actual restore GUI returns its controls to enabled state");
    if (mode == 1) {
      Check(Read(g_sDataDir / L"Sites" / L"Alpha" / L"Default" / L"state.bin") == "BACKUP" &&
            !fs::exists(restoreGuiStage), "Actual restore GUI installs verified payload and cleans its own stage");
      bool recoveryFound = false;
      for (const auto &entry : fs::directory_iterator(g_sDataDir))
        if (entry.path().filename().wstring().starts_with(L"Sites_PreRestore_") &&
            Read(entry.path() / L"Keep" / L"Default" / L"state.bin") == "ORIGINAL") recoveryFound = true;
      Check(recoveryFound && restoreGuiGuardCalled, "Actual restore GUI retains original recovery and performs final browser guard");
    } else {
      Check(Read(g_sDataDir / L"Sites" / L"Keep" / L"Default" / L"state.bin") == "ORIGINAL",
            "Actual restore GUI rejected or cancelled flow preserves original live payload");
      if (mode == 7) {
        Check(!restoreGuiStageCalled && restoreGuiMessage.find(L"Existing folders were left untouched") != std::wstring::npos,
              "Exhausted atomic staging reservation refuses before extraction");
      } else {
        Check(fs::exists(restoreGuiStage) && restoreGuiMessage.find(restoreGuiStage.wstring()) != std::wstring::npos,
              "Actual restore GUI preserves rejected staging and tells the user its exact location");
        Check(restoreGuiMessage.find(L"rolled back") == std::wstring::npos,
              "Actual restore GUI does not claim a rollback succeeded without evidence");
        if (mode == 2) {
          Check(Read(g_sDataDir / L"original-staged-payload" / L"Alpha" / L"Default" / L"state.bin") == "BACKUP",
                "Rejected GUI junction substitution retains the original validated payload");
          Check(RemoveDirectoryW((restoreGuiStage / L"Sites").c_str()) != FALSE,
                "Rejected GUI junction fixture cleanup deletes only its link object");
        }
        if (mode == 3) Check(!restoreGuiGuardCalled, "Post-extraction browser cancellation occurs before commit guard");
        if (mode == 4) Check(restoreGuiGuardCalled, "Guard-time browser cancellation exercises the sealed commit rejection consumer");
      }
    }
  }
  restoreGuiMode = 0;
}
void RenameRoundTrip(const fs::path &fixture, const wchar_t *caseName,
                     const std::wstring &oldName, const std::wstring &newName) {
  Setup(fixture, caseName, oldName);
  Check(CreateClientDesktopShortcut(oldName, BrowserKind::Edge, false), "Unicode owned shortcut created");
  std::wstring error;
  Check(RenameClientProfile(oldName, newName, error), "Unicode rename succeeds");
  Check(HasExactSafeDirectoryEntryName(g_sDataDir / L"Sites" / newName), "Requested exact Unicode directory spelling committed");
  Check(Sentinels(newName), "Unicode rename preserves notebook and both browser slots");
  Check(g_pinnedClients == std::vector<std::wstring>{newName}, "Pinned client identity migrated exactly");
  Check(g_clientsWithoutTabRestore == std::vector<std::wstring>{newName + L"|edge", newName + L"|chrome"}, "Both Unicode restore preferences migrated exactly");
  const auto shortcut = GetClientDesktopShortcutPath(newName);
  Check(shortcut && IsManagedClientDesktopShortcut(*shortcut, newName, BrowserKind::Edge), "Renamed Unicode shortcut identifies the new client");
  Check(RenameClientProfile(newName, oldName, error), "Unicode rename back succeeds");
  Check(HasExactSafeDirectoryEntryName(g_sDataDir / L"Sites" / oldName) && Sentinels(oldName), "Exact Unicode source spelling/data restored");
  Check(ArchiveClientProfile(oldName, error) && RestoreArchivedClient(oldName, &error), "Unicode archive/restore succeeds after rename");
  Check(Sentinels(oldName), "Unicode archive/restore preserves private data");
}
void UnicodeNamespaceRace(const fs::path &fixture) {
  Setup(fixture, L"unicode-namespace-race", L"M\u00FCnchen");
  SeedClient(L"Other");
  const auto other = g_sDataDir / L"Sites" / L"Other";
  Write(other / L"ctSpaces-client-notes.txt", "OTHER CLIENT NOTE");
  Write(other / L"Browsers" / L"edge" / L"Profile" / L"sentinel", "OTHER EDGE SESSION");
  Write(other / L"Browsers" / L"chrome" / L"Profile" / L"sentinel", "OTHER CHROME SESSION");
  unicodeNamespaceMoves = 0; unicodeNamespaceObserved = false; unicodeNamespaceAttack = true;
  std::wstring error;
  Check(RenameClientProfile(L"M\u00FCnchen", L"M\u00DCNCHEN", error), "Unicode case-only rename survives blocked intermediate source exchange");
  Check(unicodeNamespaceObserved && unicodeNamespaceMoves == 2 &&
      (unicodeNamespaceMutationError == ERROR_SHARING_VIOLATION || unicodeNamespaceMutationError == ERROR_ACCESS_DENIED),
      "Unicode fault injection reaches the actual second bound move and its original directory remains locked");
  Check(Sentinels(L"M\u00DCNCHEN"), "Unicode second-hop race keeps original notebook and both browser slots");
  Check(Read(other / L"ctSpaces-client-notes.txt") == "OTHER CLIENT NOTE" &&
      Read(other / L"Browsers" / L"edge" / L"Profile" / L"sentinel") == "OTHER EDGE SESSION" &&
      Read(other / L"Browsers" / L"chrome" / L"Profile" / L"sentinel") == "OTHER CHROME SESSION", "Unicode stage race never exchanges another client's notes or sessions");
}
} // namespace

int wmain() {
  const fs::path fixture = fs::current_path() / L"build" /
      (L"cu-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
  try {
    Check(std::setlocale(LC_CTYPE, "C") != nullptr, "C locale enforced for regression");
    Check(client_name_comparison::EqualIgnoringCase(L"M\u00FCnchen", L"M\u00DCNCHEN"), "Ordinal comparison handles German accented casing independently of CRT locale");
    Check(client_name_comparison::EqualIgnoringCase(L"\u00E9quipe", L"\u00C9QUIPE"), "Ordinal comparison handles French accented casing");
    Check(!client_name_comparison::EqualIgnoringCase(L"Stra\u00DFe", L"STRASSE"), "Sharp-s never expands into a different SS directory");
    Check(!client_name_comparison::EqualIgnoringCase(L"Caf\u00E9", L"Cafe\u0301"), "Distinct combining forms are not normalized together");
    Check(fs::create_directory(fixture), "Fresh disposable test directory created");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_hInst = GetModuleHandleW(nullptr); g_bQaInstance = true;
    g_sExeDir = GetCurrentExecutablePath().parent_path();
    g_hGui = CreateWindowExW(0, L"STATIC", L"Unicode rename tests", 0,
        0, 0, 1, 1, HWND_MESSAGE, nullptr, g_hInst, nullptr);
    RenameRoundTrip(fixture, L"german", L"M\u00FCnchen", L"M\u00DCNCHEN");
    RenameRoundTrip(fixture, L"french", L"\u00E9quipe", L"\u00C9QUIPE");
    RenameRoundTrip(fixture, L"japanese", L"Tokyo \u65E5\u672C", L"Tokyo \u65E5\u672C Office");
    RenameRoundTrip(fixture, L"emoji", L"Office \U0001F4BC", L"Office \U0001F4BC renamed");
    UnicodeNamespaceRace(fixture);
    Setup(fixture, L"collision", L"Stra\u00DFe"); SeedClient(L"STRASSE");
    const std::string collisionBefore = Read(g_sConfigPath);
    std::wstring error;
    Check(!RenameClientProfile(L"Stra\u00DFe", L"STRASSE", error), "Distinct sharp-s/SS client collision rejected");
    Check(Sentinels(L"Stra\u00DFe") && Sentinels(L"STRASSE") && Read(g_sConfigPath) == collisionBefore, "Sharp-s collision preserves both clients and exact settings bytes");
    Setup(fixture, L"combining", L"Caf\u00E9"); SeedClient(L"Cafe\u0301");
    Check(!RenameClientProfile(L"Caf\u00E9", L"Cafe\u0301", error), "Distinct composed/decomposed client collision rejected");
    Check(Sentinels(L"Caf\u00E9") && Sentinels(L"Cafe\u0301"), "Combining-form collision preserves both notebooks");
    Setup(fixture, L"rollback", L"M\u00FCnchen");
    const std::string before = Read(g_sConfigPath);
    HANDLE locked = CreateFileW(g_sConfigPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, 0, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "Configuration commit failure fixture locked");
    const bool renamed = RenameClientProfile(L"M\u00FCnchen", L"M\u00DCNCHEN", error);
    CloseHandle(locked);
    Check(!renamed, "Unicode case-only rename rejects a failed config commit");
    Check(HasExactSafeDirectoryEntryName(g_sDataDir / L"Sites" / L"M\u00FCnchen") && Sentinels(L"M\u00FCnchen"), "Unicode rollback restores exact source spelling and private data");
    Check(Read(g_sConfigPath) == before && g_pinnedClients == std::vector<std::wstring>{L"M\u00FCnchen"}, "Unicode rollback preserves exact saved settings and in-memory identity");
    Check(RenameClientProfile(L"M\u00FCnchen", L"M\u00DCNCHEN", error), "Unicode case-only clean retry succeeds after lock released");
    LegacyRestoreIdentity(fixture);
    GuiRestoreConsumerSafety(fixture);
    DestroyWindow(g_hGui); g_hGui = nullptr; CoUninitialize();
    fs::remove_all(fixture);
    std::cout << checks << " production Unicode rename checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL after " << checks << " checks: " << error.what()
              << "\nFixture retained: " << fixture.string() << '\n';
    if (g_hGui) DestroyWindow(g_hGui);
    return 1;
  }
}
