// Focused membership/path-budget regressions. This suite never starts the app
// entry point, creates visible windows, or permits foreground/input APIs.
#include <windows.h>
#include <shellapi.h>

static unsigned deniedPresentationCalls = 0;
static unsigned deniedLaunchCalls = 0;
static BOOL WINAPI BoundaryNoShowWindow(HWND, int) {
  ++deniedPresentationCalls; return FALSE;
}
static HWND WINAPI BoundaryNoSetFocus(HWND) {
  ++deniedPresentationCalls; return nullptr;
}
static BOOL WINAPI BoundaryNoSetForegroundWindow(HWND) {
  ++deniedPresentationCalls; return FALSE;
}
static HWND WINAPI BoundaryNoSetActiveWindow(HWND) {
  ++deniedPresentationCalls; return nullptr;
}
static BOOL WINAPI BoundarySafeSetWindowPos(HWND window, HWND after, int x,
    int y, int width, int height, UINT flags) {
  if (!(flags & SWP_NOACTIVATE) || (flags & SWP_SHOWWINDOW)) {
    ++deniedPresentationCalls; return FALSE;
  }
  return ::SetWindowPos(window, after, x, y, width, height, flags);
}
static HDWP WINAPI BoundarySafeDeferWindowPos(HDWP defer, HWND window,
    HWND after, int x, int y, int width, int height, UINT flags) {
  if (!(flags & SWP_NOACTIVATE) || (flags & SWP_SHOWWINDOW)) {
    ++deniedPresentationCalls; return nullptr;
  }
  return ::DeferWindowPos(defer, window, after, x, y, width, height, flags);
}
static HINSTANCE WINAPI BoundaryNoShellExecuteW(HWND, LPCWSTR, LPCWSTR,
    LPCWSTR, LPCWSTR, INT) {
  ++deniedLaunchCalls; return reinterpret_cast<HINSTANCE>(ERROR_ACCESS_DENIED);
}
static BOOL WINAPI BoundaryNoShellExecuteExW(SHELLEXECUTEINFOW *) {
  ++deniedLaunchCalls; SetLastError(ERROR_ACCESS_DENIED); return FALSE;
}
static BOOL WINAPI BoundaryNoCreateProcessW(LPCWSTR, LPWSTR,
    LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID,
    LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION) {
  ++deniedLaunchCalls; SetLastError(ERROR_ACCESS_DENIED); return FALSE;
}
#define ShowWindow BoundaryNoShowWindow
#define SetFocus BoundaryNoSetFocus
#define SetForegroundWindow BoundaryNoSetForegroundWindow
#define SetActiveWindow BoundaryNoSetActiveWindow
#define SetWindowPos BoundarySafeSetWindowPos
#define DeferWindowPos BoundarySafeDeferWindowPos
#define ShellExecuteW BoundaryNoShellExecuteW
#define ShellExecuteExW BoundaryNoShellExecuteExW
#define CreateProcessW BoundaryNoCreateProcessW
#define wmain ExistingIsolationFixtureEntry
#include "ClientIsolationTests.cpp"
#undef wmain

static void SeedBoundaryPreferences() {
  g_pinnedClients = {L"Bravo", L"Alpha"};
  g_clientsWithoutTabRestore = {L"Alpha|edge", L"Bravo|chrome"};
  Check(SavePinnedClients() && SaveArchivedClients() &&
        SaveRestoreTabsPreferences(), "Save initial focused preferences");
}

static void CheckMembershipBytes(const std::string &config,
    const std::string &alpha, const std::string &bravo) {
  Check(ReadBytes(g_sConfigPath) == config,
        "Rejected membership write preserves exact config bytes");
  Check(ReadBytes(g_sDataDir / L"Sites" / L"Alpha" /
        client_notes::kNotebookFileName) == alpha,
        "Membership attempt preserves Alpha rich notebook bytes");
  Check(ReadBytes(g_sDataDir / L"Sites" / L"Bravo" /
        client_notes::kNotebookFileName) == bravo,
        "Membership attempt preserves Bravo same-ID rich notebook bytes");
  Check(g_clientsWithoutTabRestore ==
        std::vector<std::wstring>{L"Alpha|edge", L"Bravo|chrome"},
        "Membership attempts preserve independent per-browser restore keys");
  OtherIntact();
}

static void MembershipFailureAndRetry() {
  Setup(L"membership-commit-retry"); SeedBoundaryPreferences();
  const auto config = ReadBytes(g_sConfigPath);
  const auto alpha = ReadBytes(g_sDataDir / L"Sites" / L"Alpha" /
                               client_notes::kNotebookFileName);
  const auto bravo = ReadBytes(g_sDataDir / L"Sites" / L"Bravo" /
                               client_notes::kNotebookFileName);
  HANDLE locked = CreateFileW(g_sConfigPath.c_str(), GENERIC_READ,
      FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  Check(locked != INVALID_HANDLE_VALUE, "Lock fixture config against replacement");
  std::wstring error;
  const bool archived = ArchiveClientProfile(L"Alpha", error);
  CloseHandle(locked);
  Check(!archived && error.find(L"atomically") != std::wstring::npos,
        "Archive reports the failed atomic membership commit");
  Check(g_archivedClients.empty() &&
        g_pinnedClients == std::vector<std::wstring>{L"Bravo", L"Alpha"},
        "Failed archive restores both membership vectors in their original order");
  CheckMembershipBytes(config, alpha, bravo);
  Check(ArchiveClientProfile(L"Alpha", error),
        "Archive clean retry succeeds after the replacement lock is released");
  Check(g_archivedClients == std::vector<std::wstring>{L"Alpha"} &&
        g_pinnedClients == std::vector<std::wstring>{L"Bravo"},
        "Successful archive moves only Alpha from pinned into archived");
  const auto archivedConfig = ReadBytes(g_sConfigPath);
  locked = CreateFileW(g_sConfigPath.c_str(), GENERIC_READ,
      FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  Check(locked != INVALID_HANDLE_VALUE, "Lock archived config against restore replacement");
  const bool restored = RestoreArchivedClient(g_archivedClients.front(), &error);
  CloseHandle(locked);
  Check(!restored && error.find(L"atomically") != std::wstring::npos,
        "Restore reports the failed atomic membership commit");
  Check(g_archivedClients == std::vector<std::wstring>{L"Alpha"} &&
        g_pinnedClients == std::vector<std::wstring>{L"Bravo"},
        "Failed restore keeps exact archive and pinned identity");
  CheckMembershipBytes(archivedConfig, alpha, bravo);
  Check(RestoreArchivedClient(g_archivedClients.front(), &error) &&
        g_archivedClients.empty() && GetClientInputText() == L"Alpha",
        "Restore clean retry keeps original aliased client selection");
  Check(ReadBytes(g_sDataDir / L"Sites" / L"Alpha" /
        client_notes::kNotebookFileName) == alpha,
        "Membership retry keeps exact original Alpha notebook");
  OtherIntact();
}

static void RejectedRenameTargets() {
  Setup(L"invalid-rename-targets"); SeedBoundaryPreferences();
  const auto config = ReadBytes(g_sConfigPath);
  const auto alpha = ReadBytes(g_sDataDir / L"Sites" / L"Alpha" /
                               client_notes::kNotebookFileName);
  const auto bravo = ReadBytes(g_sDataDir / L"Sites" / L"Bravo" /
                               client_notes::kNotebookFileName);
  std::wstring embeddedNull = L"Alpha";
  embeddedNull.push_back(0); embeddedNull += L" renamed";
  const std::vector<std::wstring> rejected = {
      L"", L" ", L".", L"..", L"../Bravo", L"Bravo\\nested", L"A:B",
      L"Alpha.", L"Alpha ", L" Alpha", L"CON", L"con.txt", L"COM1",
      L"COM\u00B9", L"LPT\u00B2.txt", L"CONIN$", L"Default", L"Temp",
      L"line\nfeed", embeddedNull, std::wstring(201, L'x'), L"Bravo"};
  for (const auto &target : rejected) {
    std::wstring error;
    Check(!RenameClientProfile(L"Alpha", target, error) && !error.empty(),
          "Invalid or colliding target rejects rename with an explanation");
    CheckMembershipBytes(config, alpha, bravo);
    Check(HasExactSafeDirectoryEntryName(g_sDataDir / L"Sites" / L"Alpha"),
          "Rejected rename preserves exact original directory spelling");
    Check(g_pinnedClients == std::vector<std::wstring>{L"Bravo", L"Alpha"} &&
          g_archivedClients.empty(),
          "Rejected rename does not rewrite membership identity");
  }
  std::wstring overBudget(180, L'z');
  const auto sites = g_sDataDir / L"Sites";
  Check(!CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(sites, overBudget),
        "Focused fixture reaches the client-root path-budget boundary");
  std::wstring error;
  Check(!RenameClientProfile(L"Alpha", overBudget, error) &&
        error.find(L"too long") != std::wstring::npos,
        "Client root location rejects an otherwise valid over-budget target");
  CheckMembershipBytes(config, alpha, bravo);

  while (!CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(sites, overBudget))
    overBudget.pop_back();
  Check(overBudget.size() > 30,
        "A longer target fits the root budget for descendant-only coverage");
  const auto originalRoot = sites / L"Alpha";
  const auto extra = originalRoot /
      std::wstring(245 - originalRoot.native().size() - 1, L'e');
  Put(extra, "PATH-BUDGET SENTINEL");
  Check(CtBackup::Detail::IsTreeTargetWithinLegacyPathBudget(originalRoot, originalRoot),
        "Original deep file stays within the legacy path budget");
  Check(!CtBackup::Detail::IsTreeTargetWithinLegacyPathBudget(originalRoot, sites / overBudget),
        "Only the longer destination makes the deep file exceed its budget");
  Check(!RenameClientProfile(L"Alpha", overBudget, error) &&
        error.find(L"path budget") != std::wstring::npos,
        "Deep descendant prevents a longer otherwise valid client rename");
  Check(ReadBytes(extra) == "PATH-BUDGET SENTINEL",
        "Over-budget rename preserves the original deep descendant");
  CheckMembershipBytes(config, alpha, bravo);
}

int wmain() {
  fixture = fs::current_path() / L"build" /
      (L"clb-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
       std::to_wstring(GetTickCount64()));
  try {
    Check(CTSPACES_VERSION_TEXT == std::string("6.3.3.0"),
          "Focused audit keeps version 6.3.3.0");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_hInst = GetModuleHandleW(nullptr); g_bQaInstance = true;
    g_sExeDir = GetCurrentExecutablePath().parent_path();
    g_hGui = CreateWindowExW(0, L"STATIC", L"Client boundary message-only fixture",
        0, 0, 0, 1, 1, HWND_MESSAGE, nullptr, g_hInst, nullptr);
    g_hClientEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD,
        0, 0, 1, 1, g_hGui, nullptr, g_hInst, nullptr);
    g_hComboClient = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWN,
        0, 0, 1, 1, g_hGui, nullptr, g_hInst, nullptr);
    Check(g_hGui && g_hClientEdit && g_hComboClient && !IsWindowVisible(g_hGui),
          "Only hidden message-only fixture controls exist");
    MembershipFailureAndRetry(); RejectedRenameTargets();
    Check(deniedLaunchCalls == 0,
          "Selected native tests never request a process or browser launch");
    Check(!IsWindowVisible(g_hGui), "Fixture stays invisible throughout the run");
    DestroyWindow(g_hGui); g_hGui = nullptr; CoUninitialize();
    std::wcout << L"Client lifecycle boundaries passed: " << checks
               << L" assertions, " << scenarios << L" disposable scenarios; "
               << deniedPresentationCalls << L" presentation calls blocked, "
               << deniedLaunchCalls << L" launch calls. Fixture retained: "
               << fixture << L'\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Client lifecycle boundary failure: " << error.what() << '\n';
    std::wcout << L"Fixture retained: " << fixture << L'\n';
    if (g_hGui) DestroyWindow(g_hGui); CoUninitialize(); return 1;
  }
}
