#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace installer_test_boundary {
BOOL WINAPI ShellExecuteExW(SHELLEXECUTEINFOW *executeInfo);
LSTATUS WINAPI RegOpenKeyExW(HKEY key, LPCWSTR subKey, DWORD options,
                             REGSAM desiredAccess, PHKEY result);
LSTATUS WINAPI RegCreateKeyExW(HKEY key, LPCWSTR subKey, DWORD reserved,
                               LPWSTR className, DWORD options,
                               REGSAM desiredAccess,
                               const LPSECURITY_ATTRIBUTES securityAttributes,
                               PHKEY result, LPDWORD disposition);
LSTATUS WINAPI RegSetValueExW(HKEY key, LPCWSTR valueName, DWORD reserved,
                              DWORD type, const BYTE *data, DWORD byteCount);
LSTATUS WINAPI RegDeleteValueW(HKEY key, LPCWSTR valueName);
HRESULT WINAPI SHGetKnownFolderPath(REFKNOWNFOLDERID folderId, DWORD flags,
                                    HANDLE token, PWSTR *path);
HANDLE WINAPI CreateFileW(LPCWSTR fileName, DWORD desiredAccess,
                          DWORD shareMode,
                          LPSECURITY_ATTRIBUTES securityAttributes,
                          DWORD creationDisposition, DWORD flagsAndAttributes,
                          HANDLE templateFile);
HANDLE WINAPI CreateMutexW(LPSECURITY_ATTRIBUTES attributes,
                           BOOL initialOwner, LPCWSTR name);
HWND WINAPI FindWindowW(LPCWSTR className, LPCWSTR windowName);
} // namespace installer_test_boundary

// Compile the production translation unit into the harness. Only the explicitly
// named operating-system boundaries below are redirected. The installer/update
// control flow, validation, staging, byte copy, verification, retry, and atomic
// MoveFileExW commit are the production implementations.
#define CTSPACES_INSTALLER_TEST_HOOKS
#define wWinMain CtSpacesProductionWinMain
#define ShellExecuteExW installer_test_boundary::ShellExecuteExW
#define RegOpenKeyExW installer_test_boundary::RegOpenKeyExW
#define RegCreateKeyExW installer_test_boundary::RegCreateKeyExW
#define RegSetValueExW installer_test_boundary::RegSetValueExW
#define RegDeleteValueW installer_test_boundary::RegDeleteValueW
#define SHGetKnownFolderPath installer_test_boundary::SHGetKnownFolderPath
#define CreateFileW installer_test_boundary::CreateFileW
#define CreateMutexW installer_test_boundary::CreateMutexW
#define FindWindowW installer_test_boundary::FindWindowW
#include "../ctSpaces.cpp"
#undef FindWindowW
#undef CreateMutexW
#undef CreateFileW
#undef SHGetKnownFolderPath
#undef RegDeleteValueW
#undef RegSetValueExW
#undef RegCreateKeyExW
#undef RegOpenKeyExW
#undef ShellExecuteExW
#undef wWinMain

namespace {

struct CapturedMessage {
  std::wstring text;
  std::wstring title;
  UINT flags = 0;
};

std::vector<int> g_answers;
size_t g_nextAnswer = 0;
std::vector<CapturedMessage> g_messages;
bool g_overrideVersions = false;
std::wstring g_currentVersion;
std::wstring g_installedVersion;
fs::path g_currentExecutable;
fs::path g_expectedInstallDirectory;
bool g_launchShouldSucceed = true;
bool g_launchUseSuspendedProbeChild = false;
HANDLE g_probeProcess = nullptr;
HANDLE g_probeThread = nullptr;
unsigned g_launchCalls = 0;
unsigned g_registryReads = 0;
unsigned g_forbiddenRegistryWrites = 0;
unsigned g_knownFolderCalls = 0;
bool g_failStageCreation = false;
bool g_armLateClientDeleteLock = false;
fs::path g_lateClientDeleteLockPath;
HANDLE g_lateClientDeleteLock = nullptr;
bool g_handoffMode = false;
std::wstring g_handoffToken;
fs::path g_handoffLocalAppData;

std::wstring ReadEnvironment(const wchar_t *name) {
  const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
  if (required == 0)
    return {};
  std::vector<wchar_t> value(required, L'\0');
  const DWORD written =
      GetEnvironmentVariableW(name, value.data(), required);
  if (written == 0 || written >= required)
    return {};
  return std::wstring(value.data(), written);
}

bool LoadHandoffEnvironment() {
  const std::wstring root =
      ReadEnvironment(L"CTSPACES_INSTALLER_TEST_LOCALAPPDATA");
  const std::wstring token =
      ReadEnvironment(L"CTSPACES_INSTALLER_TEST_TOKEN");
  if (root.empty() || token.empty() || token.size() > 40)
    return false;
  const fs::path candidate(root);
  if (!candidate.is_absolute() || !IsSafeExistingDirectory(candidate))
    return false;
  g_handoffMode = true;
  g_handoffToken = token;
  g_handoffLocalAppData = fs::absolute(candidate).lexically_normal();
  g_expectedInstallDirectory =
      g_handoffLocalAppData / L"InfinitySys" / L"ctSpaces";
  return true;
}

[[noreturn]] void Fail(const std::string &message) {
  throw std::runtime_error(message);
}

void Check(bool condition, const std::string &message) {
  if (!condition)
    Fail(message);
}

bool SamePath(const fs::path &left, const fs::path &right) {
  return _wcsicmp(fs::absolute(left).lexically_normal().c_str(),
                  fs::absolute(right).lexically_normal().c_str()) == 0;
}

std::vector<unsigned char> ReadBytes(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    Fail("could not open fixture file for reading");
  return std::vector<unsigned char>(std::istreambuf_iterator<char>(stream),
                                    std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path &path, const std::string &value) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream)
    Fail("could not open fixture file for writing");
  stream.write(value.data(), static_cast<std::streamsize>(value.size()));
  if (!stream)
    Fail("could not write fixture file");
}

class Fixture {
public:
  explicit Fixture(const wchar_t *caseName) {
    wchar_t tempPath[MAX_PATH + 1]{};
    const DWORD length = GetTempPathW(MAX_PATH, tempPath);
    Check(length != 0 && length < MAX_PATH, "GetTempPathW failed");
    GUID guid{};
    Check(SUCCEEDED(CoCreateGuid(&guid)), "CoCreateGuid failed");
    tempBase_ = fs::absolute(fs::path(tempPath)).lexically_normal();
    if (tempBase_.filename().empty())
      tempBase_ = tempBase_.parent_path();
    Check(IsSafeExistingDirectory(tempBase_),
          "temporary base is not a safe regular directory");
    root_ = tempBase_ /
            std::format(L"ctSpaces-installer-runtime-{}-{}", caseName,
                        FormatStageCollisionToken(guid));
    Check(fs::create_directory(root_),
          "unique installer fixture directory already existed");
    installDirectory_ = root_ / L"appdata";
    Check(fs::create_directory(installDirectory_),
          "could not create fixture application-data directory");
    clientsDirectory_ = installDirectory_ / L"Clients";
    Check(fs::create_directory(clientsDirectory_),
          "could not create legacy client-sentinel directory");
    sentinel_ = clientsDirectory_ / L"do-not-touch.txt";
    WriteBytes(sentinel_, "client-sentinel");
    configSentinel_ = installDirectory_ / L"config.ini";
    WriteBytes(configSentinel_, "[fixture]\r\nkeep=1\r\n");
    sitesDirectory_ = installDirectory_ / L"Sites";
    Check(fs::create_directory(sitesDirectory_),
          "could not create fixture Sites directory");
    preservedClient_ = sitesDirectory_ / L"KeepMe";
    Check(fs::create_directory(preservedClient_),
          "could not create preserved v2 client fixture");
    WriteBytes(preservedClient_ / kClientSchemaMarkerName,
               kClientSchemaMarkerText);
    siteSentinel_ = preservedClient_ / L"state.dat";
    WriteBytes(siteSentinel_, "site-client-sentinel");
    g_sDataDir = installDirectory_;
    g_expectedInstallDirectory = installDirectory_;
  }

  ~Fixture() {
    std::error_code error;
    const fs::path checkedRoot = fs::absolute(root_).lexically_normal();
    const DWORD attributes = GetFileAttributesW(checkedRoot.c_str());
    if (!checkedRoot.empty() && checkedRoot.parent_path() == tempBase_ &&
        checkedRoot.filename().wstring().starts_with(
            L"ctSpaces-installer-runtime-") &&
        attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
      fs::remove_all(checkedRoot, error);
    }
  }

  const fs::path &installDirectory() const { return installDirectory_; }
  fs::path installedExe() const {
    return installDirectory_ / L"ctSpaces.exe";
  }
  const fs::path &sentinel() const { return sentinel_; }

  void SeedInstalled(const std::string &bytes = "old-installed-build") const {
    WriteBytes(installedExe(), bytes);
  }

  void CheckSentinelsAndNoStageLitter(bool allowEmptyDeleteBase = false) const {
    Check(ReadBytes(sentinel_) ==
              std::vector<unsigned char>({'c', 'l', 'i', 'e', 'n', 't', '-',
                                          's', 'e', 'n', 't', 'i', 'n', 'e',
                                          'l'}),
          "client sentinel was changed");
    Check(ReadBytes(configSentinel_) ==
              std::vector<unsigned char>({'[', 'f', 'i', 'x', 't', 'u', 'r',
                                          'e', ']', '\r', '\n', 'k', 'e', 'e',
                                          'p', '=', '1', '\r', '\n'}),
          "config.ini sentinel was changed");
    Check(ReadBytes(siteSentinel_) ==
              std::vector<unsigned char>({'s', 'i', 't', 'e', '-', 'c', 'l',
                                          'i', 'e', 'n', 't', '-', 's', 'e',
                                          'n', 't', 'i', 'n', 'e', 'l'}),
          "Sites/client sentinel was changed");
    Check(MarkerHasExactContents(preservedClient_ / kClientSchemaMarkerName,
                                 kClientSchemaMarkerText),
          "preserved v2 client schema marker was changed");
    for (const auto &entry : fs::directory_iterator(installDirectory_)) {
      const std::wstring name = entry.path().filename().wstring();
      Check(_wcsicmp(name.c_str(), L"ctSpaces.exe") == 0 ||
                _wcsicmp(name.c_str(), L"config.ini") == 0 ||
                _wcsicmp(name.c_str(), L"Clients") == 0 ||
                _wcsicmp(name.c_str(), L"Sites") == 0 ||
                (allowEmptyDeleteBase &&
                 _wcsicmp(name.c_str(), L"_Delete") == 0 &&
                 IsSafeExistingDirectory(entry.path()) &&
                 fs::is_empty(entry.path())),
            "installer left an unexpected sibling staging file");
    }
    Check(g_forbiddenRegistryWrites == 0,
          "installer attempted to escape through a real registry write path");
  }

private:
  fs::path root_;
  fs::path tempBase_;
  fs::path installDirectory_;
  fs::path clientsDirectory_;
  fs::path sentinel_;
  fs::path configSentinel_;
  fs::path sitesDirectory_;
  fs::path preservedClient_;
  fs::path siteSentinel_;
};

void ResetBoundaries(std::initializer_list<int> answers = {}) {
  g_answers.assign(answers.begin(), answers.end());
  g_nextAnswer = 0;
  g_messages.clear();
  g_overrideVersions = false;
  g_currentVersion.clear();
  g_installedVersion.clear();
  g_launchShouldSucceed = true;
  Check(g_probeProcess == nullptr && g_probeThread == nullptr,
        "previous launch probe was not cleaned up");
  g_launchUseSuspendedProbeChild = false;
  g_launchCalls = 0;
  g_registryReads = 0;
  g_forbiddenRegistryWrites = 0;
  g_knownFolderCalls = 0;
  g_failStageCreation = false;
  Check(g_lateClientDeleteLock == nullptr,
        "previous late client-delete lock was not released");
  g_armLateClientDeleteLock = false;
  g_lateClientDeleteLockPath.clear();
}

void SetVersions(std::wstring current, std::wstring installed) {
  g_overrideVersions = true;
  g_currentVersion = std::move(current);
  g_installedVersion = std::move(installed);
}

bool HasMessage(const wchar_t *titlePart, const wchar_t *textPart) {
  return std::ranges::any_of(g_messages, [&](const CapturedMessage &message) {
    return message.title.find(titlePart) != std::wstring::npos &&
           message.text.find(textPart) != std::wstring::npos;
  });
}

void CheckInstalledMatchesCurrent(const Fixture &fixture) {
  Check(ReadBytes(fixture.installedExe()) == ReadBytes(g_currentExecutable),
        "installed executable is not byte-for-byte equal to the running "
        "harness image");
  fixture.CheckSentinelsAndNoStageLitter();
}

void TestRealVersionResourceReader() {
  ResetBoundaries();
  const std::wstring version = GetExeVersion(g_currentExecutable);
  Check(version == CTSPACES_VERSION_WTEXT,
        "real GetExeVersion did not read the harness VERSIONINFO resource");
}

void TestFreshInstallSuccess() {
  Fixture fixture(L"fresh-success");
  ResetBoundaries({IDYES, IDNO, IDNO, IDNO});
  const bool keepRunning = doInstall();
  Check(!keepRunning,
        "fresh install should stop current copy after checked launch succeeds");
  CheckInstalledMatchesCurrent(fixture);
  Check(g_launchCalls == 1, "fresh install did not request one relaunch");
  Check(g_registryReads == 1,
        "fresh install did not inspect the redirected autorun boundary once");
  Check(g_knownFolderCalls == 0,
        "declined shortcuts unexpectedly resolved a real known folder");
}

void TestInstallDeclines() {
  {
    Fixture fixture(L"decline-run-once");
    ResetBoundaries({IDNO, IDYES});
    Check(doInstall(), "declined install/run-once yes should keep running");
    Check(!fs::exists(fixture.installedExe()),
          "declined install created an executable");
    fixture.CheckSentinelsAndNoStageLitter();
  }
  {
    Fixture fixture(L"decline-exit");
    ResetBoundaries({IDNO, IDNO});
    Check(!doInstall(), "declined install/run-once no should exit");
    Check(!fs::exists(fixture.installedExe()),
          "declined install created an executable");
    fixture.CheckSentinelsAndNoStageLitter();
  }
}

void TestNewerUpdateSuccess() {
  Fixture fixture(L"newer-success");
  fixture.SeedInstalled();
  ResetBoundaries({IDYES});
  SetVersions(L"9.0.0.0", L"8.0.0.0");
  Check(!chkUpdate(),
        "accepted newer update should stop after checked launch succeeds");
  CheckInstalledMatchesCurrent(fixture);
  Check(g_launchCalls == 1, "accepted newer update did not relaunch");
}

void TestNewerUpdateDeclines() {
  {
    Fixture fixture(L"newer-decline-run");
    fixture.SeedInstalled("old-newer-decline-run");
    const auto before = ReadBytes(fixture.installedExe());
    ResetBoundaries({IDNO, IDYES});
    SetVersions(L"9.0.0.0", L"8.0.0.0");
    Check(chkUpdate(), "declined update/run-once yes should keep running");
    Check(ReadBytes(fixture.installedExe()) == before,
          "declined newer update changed installed bytes");
    fixture.CheckSentinelsAndNoStageLitter();
  }
  {
    Fixture fixture(L"newer-decline-exit");
    fixture.SeedInstalled("old-newer-decline-exit");
    const auto before = ReadBytes(fixture.installedExe());
    ResetBoundaries({IDNO, IDNO});
    SetVersions(L"9.0.0.0", L"8.0.0.0");
    Check(!chkUpdate(), "declined update/run-once no should exit");
    Check(ReadBytes(fixture.installedExe()) == before,
          "declined newer update changed installed bytes");
    fixture.CheckSentinelsAndNoStageLitter();
  }
}

void TestEqualVersionChoices() {
  {
    Fixture fixture(L"equal-replace");
    fixture.SeedInstalled();
    ResetBoundaries({IDYES});
    SetVersions(L"8.0.0.0", L"8.0.0.0");
    Check(!chkUpdate(), "accepted equal-version replacement should relaunch");
    CheckInstalledMatchesCurrent(fixture);
  }
  {
    Fixture fixture(L"equal-decline");
    fixture.SeedInstalled("equal-old");
    const auto before = ReadBytes(fixture.installedExe());
    ResetBoundaries({IDNO, IDYES});
    SetVersions(L"8.0.0.0", L"8.0.0.0");
    Check(chkUpdate(),
          "declined equal replacement/run-once yes should keep running");
    Check(ReadBytes(fixture.installedExe()) == before,
          "declined equal replacement changed installed bytes");
    fixture.CheckSentinelsAndNoStageLitter();
  }
}

void TestOlderVersionChoices() {
  {
    Fixture fixture(L"older-run");
    fixture.SeedInstalled("new-installed");
    const auto before = ReadBytes(fixture.installedExe());
    ResetBoundaries({IDYES});
    SetVersions(L"7.0.0.0", L"8.0.0.0");
    Check(chkUpdate(), "older run-once yes should keep running");
    Check(ReadBytes(fixture.installedExe()) == before,
          "older-copy choice changed installed bytes");
    fixture.CheckSentinelsAndNoStageLitter();
  }
  {
    Fixture fixture(L"older-exit");
    fixture.SeedInstalled("new-installed");
    const auto before = ReadBytes(fixture.installedExe());
    ResetBoundaries({IDNO});
    SetVersions(L"7.0.0.0", L"8.0.0.0");
    Check(!chkUpdate(), "older run-once no should exit");
    Check(ReadBytes(fixture.installedExe()) == before,
          "older-copy rejection changed installed bytes");
    fixture.CheckSentinelsAndNoStageLitter();
  }
}

void TestStagingFailurePreservesState() {
  Fixture fixture(L"stage-failure");
  fixture.SeedInstalled("stage-old-installed");
  const auto before = ReadBytes(fixture.installedExe());
  ResetBoundaries({IDYES});
  SetVersions(L"9.0.0.0", L"8.0.0.0");
  g_failStageCreation = true;
  Check(chkUpdate(), "staging failure should keep current copy running");
  Check(ReadBytes(fixture.installedExe()) == before,
        "staging failure changed installed bytes");
  fixture.CheckSentinelsAndNoStageLitter();
  Check(g_launchCalls == 0, "staging failure attempted a relaunch");
  Check(HasMessage(L"Update ctSpaces", L"staging file could not be created"),
        "staging failure did not surface its explicit Windows error");
}

void TestLockedDestinationPreservesState() {
  Fixture fixture(L"locked-destination");
  fixture.SeedInstalled("locked-old-installed");
  const auto before = ReadBytes(fixture.installedExe());
  HANDLE lock = ::CreateFileW(fixture.installedExe().c_str(), GENERIC_READ,
                              FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  Check(lock != INVALID_HANDLE_VALUE, "could not lock installed fixture");
  ResetBoundaries({IDYES});
  SetVersions(L"9.0.0.0", L"8.0.0.0");
  const bool keepRunning = chkUpdate();
  CloseHandle(lock);
  Check(keepRunning, "locked destination should keep current copy running");
  Check(ReadBytes(fixture.installedExe()) == before,
        "locked destination changed installed bytes");
  fixture.CheckSentinelsAndNoStageLitter();
  Check(g_launchCalls == 0, "locked destination attempted a relaunch");
  Check(HasMessage(L"Update ctSpaces", L"Atomic replacement failed"),
        "locked destination did not surface the atomic replacement error");
}

void TestLaunchFailureIsExplicit() {
  Fixture fixture(L"launch-failure");
  fixture.SeedInstalled("launch-old-installed");
  ResetBoundaries({IDYES});
  SetVersions(L"9.0.0.0", L"8.0.0.0");
  g_launchShouldSucceed = false;
  Check(chkUpdate(), "launch failure should keep current copy running");
  CheckInstalledMatchesCurrent(fixture);
  Check(g_launchCalls == 1, "launch-failure case did not attempt launch once");
  Check(HasMessage(L"Update ctSpaces",
                   L"updated, but Windows could not start it"),
        "launch failure lacked an explicit update warning");
  Check(HasMessage(L"Update ctSpaces", L"This copy will continue running"),
        "launch failure did not say that this copy continues running");
}

void TestLaunchUsesARealLiveProcessHandle() {
  Fixture fixture(L"live-process-handle");
  fixture.SeedInstalled("launch-validation-placeholder");
  ResetBoundaries();
  g_launchUseSuspendedProbeChild = true;
  std::wstring launchError;
  Check(LaunchInstalledVersion(fixture.installedExe(), &launchError),
        "production launch validation rejected a real live process handle");
  Check(g_probeProcess != nullptr && g_probeThread != nullptr,
        "launch probe did not retain its isolated child handles");
  Check(ResumeThread(g_probeThread) != static_cast<DWORD>(-1),
        "could not resume isolated launch-probe child");
  Check(WaitForSingleObject(g_probeProcess, 5000) == WAIT_OBJECT_0,
        "isolated launch-probe child did not exit");
  CloseHandle(g_probeThread);
  CloseHandle(g_probeProcess);
  g_probeThread = nullptr;
  g_probeProcess = nullptr;
  fixture.CheckSentinelsAndNoStageLitter();
}

void TestAlreadyInstalledPath() {
  Fixture fixture(L"already-installed");
  std::error_code error;
  fs::copy_file(g_currentExecutable, fixture.installedExe(),
                fs::copy_options::overwrite_existing, error);
  Check(!error, "could not seed already-installed hard-link fixture");
  fs::remove(fixture.installedExe(), error);
  Check(!error, "could not prepare already-installed hard link");
  fs::create_hard_link(g_currentExecutable, fixture.installedExe(), error);
  Check(!error, "could not create already-installed hard link");
  ResetBoundaries();
  Check(chkUpdate(), "installed executable should run without prompting");
  Check(g_messages.empty(), "installed executable path unexpectedly prompted");
  fixture.CheckSentinelsAndNoStageLitter();
}

HWND FindMainWindowForProcess(DWORD processId) {
  struct Search {
    DWORD processId = 0;
    HWND window = nullptr;
  } search{processId, nullptr};
  EnumWindows(
      [](HWND window, LPARAM parameter) -> BOOL {
        auto *search = reinterpret_cast<Search *>(parameter);
        DWORD owner = 0;
        GetWindowThreadProcessId(window, &owner);
        wchar_t className[128]{};
        GetClassNameW(window, className, static_cast<int>(std::size(className)));
        if (owner == search->processId &&
            wcscmp(className, GUI_CLASS_NAME.c_str()) == 0) {
          search->window = window;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&search));
  return search.window;
}

void TestRealInstalledChildReadinessHandoff() {
  wchar_t tempPath[MAX_PATH + 1]{};
  const DWORD tempLength = GetTempPathW(MAX_PATH, tempPath);
  Check(tempLength != 0 && tempLength < MAX_PATH,
        "handoff GetTempPathW failed");
  const fs::path tempBase =
      [&]() {
        fs::path value = fs::absolute(fs::path(tempPath)).lexically_normal();
        return value.filename().empty() ? value.parent_path() : value;
      }();
  GUID guid{};
  Check(SUCCEEDED(CoCreateGuid(&guid)), "handoff CoCreateGuid failed");
  const std::wstring token = FormatStageCollisionToken(guid);
  const fs::path root =
      tempBase / (L"ctSpaces-installer-handoff-" + token);
  Check(fs::create_directory(root),
        "unique installer handoff fixture already existed");
  const fs::path localAppData = root / L"LocalAppData";
  Check(fs::create_directory(localAppData),
        "could not create handoff LocalAppData fixture");

  const std::wstring oldRoot =
      ReadEnvironment(L"CTSPACES_INSTALLER_TEST_LOCALAPPDATA");
  const std::wstring oldToken =
      ReadEnvironment(L"CTSPACES_INSTALLER_TEST_TOKEN");
  SetEnvironmentVariableW(L"CTSPACES_INSTALLER_TEST_LOCALAPPDATA",
                          localAppData.c_str());
  SetEnvironmentVariableW(L"CTSPACES_INSTALLER_TEST_TOKEN", token.c_str());

  HANDLE updaterProcess = nullptr;
  HANDLE childProcess = nullptr;
  DWORD childId = 0;
  const auto cleanup = [&]() {
    if (childProcess) {
      if (WaitForSingleObject(childProcess, 0) == WAIT_TIMEOUT)
        TerminateProcess(childProcess, 197);
      WaitForSingleObject(childProcess, 5000);
      CloseHandle(childProcess);
      childProcess = nullptr;
    }
    if (updaterProcess) {
      if (WaitForSingleObject(updaterProcess, 0) == WAIT_TIMEOUT)
        TerminateProcess(updaterProcess, 198);
      WaitForSingleObject(updaterProcess, 5000);
      CloseHandle(updaterProcess);
      updaterProcess = nullptr;
    }
    SetEnvironmentVariableW(L"CTSPACES_INSTALLER_TEST_LOCALAPPDATA",
                            oldRoot.empty() ? nullptr : oldRoot.c_str());
    SetEnvironmentVariableW(L"CTSPACES_INSTALLER_TEST_TOKEN",
                            oldToken.empty() ? nullptr : oldToken.c_str());
    const fs::path checkedRoot = fs::absolute(root).lexically_normal();
    const DWORD attributes = GetFileAttributesW(checkedRoot.c_str());
    if (checkedRoot.parent_path() == tempBase &&
        checkedRoot.filename().wstring().starts_with(
            L"ctSpaces-installer-handoff-") &&
        attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
      std::error_code cleanupError;
      for (unsigned attempt = 0; attempt < 20; ++attempt) {
        cleanupError.clear();
        fs::remove_all(checkedRoot, cleanupError);
        std::error_code existsError;
        if (!fs::exists(checkedRoot, existsError) && !existsError)
          break;
        Sleep(100);
      }
    }
  };

  try {
    std::wstring commandLine = L"\"" + g_currentExecutable.wstring() +
                               L"\" --installer-handoff-updater";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION updater{};
    Check(::CreateProcessW(g_currentExecutable.c_str(), commandLine.data(),
                           nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                           root.c_str(), &startup, &updater) != FALSE,
          "could not start isolated updater process");
    updaterProcess = updater.hProcess;
    CloseHandle(updater.hThread);
    Check(WaitForSingleObject(updaterProcess, 30000) == WAIT_OBJECT_0,
          "isolated updater did not exit after handoff");
    DWORD updaterExit = 0;
    Check(GetExitCodeProcess(updaterProcess, &updaterExit) && updaterExit == 0,
          "isolated updater returned a failure exit code");

    const fs::path pidPath = localAppData / L"handoff-child.pid";
    std::ifstream pidFile(pidPath);
    pidFile >> childId;
    Check(pidFile.good() || pidFile.eof(),
          "handoff child PID was not recorded");
    pidFile.close();
    Check(!pidFile.fail(), "handoff child PID file could not be closed");
    Check(childId != 0, "handoff child PID was invalid");
    childProcess = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                               FALSE, childId);
    Check(childProcess != nullptr, "could not open installed handoff child");

    HWND mainWindow = nullptr;
    for (unsigned attempt = 0; attempt < 300 && !mainWindow; ++attempt) {
      Check(WaitForSingleObject(childProcess, 0) == WAIT_TIMEOUT,
            "installed handoff child exited before window readiness");
      mainWindow = FindMainWindowForProcess(childId);
      if (!mainWindow)
        Sleep(100);
    }
    Check(mainWindow != nullptr,
          "installed handoff child did not create its production main window");
    Check(GetDlgItem(mainWindow, 102) != nullptr,
          "installed handoff child did not create the client combo control");
    Check(GetDlgItem(mainWindow, IDC_CLIENT_EDIT) != nullptr,
          "installed handoff child did not create the client edit control");
    DWORD_PTR response = 0;
    Check(SendMessageTimeoutW(mainWindow, WM_NULL, 0, 0,
                              SMTO_ABORTIFHUNG | SMTO_BLOCK, 3000,
                              &response) != 0,
          "installed handoff child main window was unresponsive");

    const fs::path installed = localAppData / L"InfinitySys" / L"ctSpaces" /
                               L"ctSpaces.exe";
    Check(ReadBytes(installed) == ReadBytes(g_currentExecutable),
          "real handoff child image was not the copied harness executable");
    const fs::path registryEscape =
        localAppData / L"registry-write-attempted";
    Check(!fs::exists(registryEscape),
          "handoff attempted a forbidden registry mutation");

    PostMessageW(mainWindow, WM_CLOSE, 0, 0);
    Check(WaitForSingleObject(childProcess, 10000) == WAIT_OBJECT_0,
          "installed handoff child did not close cleanly");
    DWORD childExit = 0;
    Check(GetExitCodeProcess(childProcess, &childExit) && childExit == 0,
          "installed handoff child returned a failure exit code");
    cleanup();
    Check(!fs::exists(root),
          "isolated installed-child handoff fixture was not removed after "
          "closing all harness handles");
  } catch (...) {
    cleanup();
    throw;
  }
}

void TestDefaultCloseKeepsConcurrentLaunchBusy() {
  Fixture fixture(L"default-close-busy");
  const fs::path defaultRoot = fixture.installDirectory() / L"Default";
  Check(fs::create_directory(defaultRoot),
        "could not create disposable Default profile fixture");
  WriteBytes(defaultRoot / L"state.dat", "default-state");
  ResetBoundaries({IDNO});
  g_isLaunchInFlight = true;
  g_bUiEnabled = false;
  HandleClosedDefaultProfile();
  Check(!g_bUiEnabled,
        "Default close re-enabled UI during a concurrent launch worker");
  g_isLaunchInFlight = false;
  SetUiState(true);
  Check(g_bUiEnabled,
        "UI did not re-enable after Default handling and launch completed");

  g_bDefaultProfileUiBusy = true;
  g_isLaunchInFlight = false;
  SetUiState(true);
  Check(!g_bUiEnabled,
        "task completion re-enabled UI inside Default-profile busy scope");
  g_bDefaultProfileUiBusy = false;
  SetUiState(true);
  Check(g_bUiEnabled,
        "UI did not re-enable after Default-profile busy scope ended");
  fixture.CheckSentinelsAndNoStageLitter();
}

void TestLockedClientDeleteCanBeRetried() {
  Fixture fixture(L"locked-client-delete");
  const fs::path clientRoot =
      fixture.installDirectory() / L"Sites" / L"DeleteMe";
  Check(fs::create_directory(clientRoot),
        "could not create delete-client fixture");
  WriteBytes(clientRoot / kClientSchemaMarkerName, kClientSchemaMarkerText);
  const fs::path slotRoot = clientRoot / L"Browsers" / L"Edge";
  const fs::path profileRoot = slotRoot / L"Profile";
  Check(fs::create_directories(profileRoot),
        "could not create v2 browser slot fixture");
  WriteBytes(slotRoot / kBrowserSchemaMarkerName,
             GetBrowserMarkerText(BrowserKind::Edge));
  WriteBytes(profileRoot / L"ctSpaces", "ctSpaces-profile=2\r\n");
  const fs::path lockedPath = profileRoot / L"locked-state.dat";
  WriteBytes(lockedPath, "locked-client-state");
  const client_activity::Record oldActivity{
      client_activity::ThreeMonthsBefore(client_activity::Now()) - 1, false};
  Check(client_activity::Write(clientRoot, oldActivity),
        "could not write old activity fixture");
  const fs::path activityPath = clientRoot / client_activity::kFileName;

  const auto clientMarkerBefore =
      ReadBytes(clientRoot / kClientSchemaMarkerName);
  const auto browserMarkerBefore =
      ReadBytes(slotRoot / kBrowserSchemaMarkerName);
  const auto profileMarkerBefore = ReadBytes(profileRoot / L"ctSpaces");
  const auto activityBefore = ReadBytes(activityPath);
  WIN32_FILE_ATTRIBUTE_DATA activityInfoBefore{};
  Check(GetFileAttributesExW(activityPath.c_str(), GetFileExInfoStandard,
                             &activityInfoBefore) != FALSE,
        "could not inspect old activity fixture");

  std::wstring probeDetails;
  const ProfileUseState probe =
      ProbeClientProfilesInUse(L"DeleteMe", probeDetails);
  Check(probe != ProfileUseState::Indeterminate,
        "locked-client deletion setup blocked: browser process probe was "
        "indeterminate");
  Check(probe == ProfileUseState::NotInUse,
        "locked-client deletion setup blocked: fixture profile is in use");

  g_lateClientDeleteLockPath = lockedPath;
  g_armLateClientDeleteLock = true;
  std::wstring firstOutcome;
  const bool firstDeleted =
      DeleteEntireClient(L"DeleteMe", true, oldActivity, firstOutcome);
  Check(g_lateClientDeleteLock != nullptr,
        "post-preflight hook did not acquire the late deletion lock");
  CloseHandle(g_lateClientDeleteLock);
  g_lateClientDeleteLock = nullptr;
  Check(!firstDeleted,
        "locked client deletion incorrectly reported complete success");
  Check(firstOutcome.find(L"ownership metadata was retained") !=
            std::wstring::npos,
        "late locked deletion did not report retryable metadata retention");
  Check(ReadBytes(clientRoot / kClientSchemaMarkerName) == clientMarkerBefore,
        "late deletion race changed the client ownership marker");
  Check(ReadBytes(slotRoot / kBrowserSchemaMarkerName) == browserMarkerBefore,
        "late deletion race changed the browser ownership marker");
  Check(ReadBytes(profileRoot / L"ctSpaces") == profileMarkerBefore,
        "late deletion race changed the Chromium profile marker");
  Check(ReadBytes(activityPath) == activityBefore,
        "late deletion race changed the client activity bytes");
  Check(client_activity::Read(clientRoot) ==
            std::optional<client_activity::Record>(oldActivity),
        "late deletion race changed the decoded client activity age");
  WIN32_FILE_ATTRIBUTE_DATA activityInfoAfter{};
  Check(GetFileAttributesExW(activityPath.c_str(), GetFileExInfoStandard,
                             &activityInfoAfter) != FALSE &&
            CompareFileTime(&activityInfoBefore.ftLastWriteTime,
                            &activityInfoAfter.ftLastWriteTime) == 0,
        "late deletion race rewrote the client activity timestamp");
  fs::path stillValidated;
  std::wstring stillValidDetails;
  Check(ValidateWholeClientDeleteTarget(L"DeleteMe", clientRoot,
                                        stillValidated,
                                        stillValidDetails),
        "late deletion race destroyed retry validation authority");

  std::wstring remaining;
  if (fs::exists(clientRoot)) {
    for (const auto &entry : fs::recursive_directory_iterator(clientRoot)) {
      if (!remaining.empty())
        remaining += L", ";
      remaining += fs::relative(entry.path(), clientRoot).wstring();
    }
  }
  std::wcout << L"Locked-delete remaining tree: "
             << (remaining.empty() ? L"<none>" : remaining) << L"\n";
  fixture.CheckSentinelsAndNoStageLitter(true);

  std::wstring secondOutcome;
  const bool secondDeleted =
      DeleteEntireClient(L"DeleteMe", true, oldActivity, secondOutcome);
  std::wcout << L"Locked-delete retry outcome: " << secondOutcome << L"\n";
  Check(secondDeleted,
        "client deletion could not be retried after releasing the file lock");
  Check(!fs::exists(clientRoot),
        "retry reported success but the client root still exists");
  Check(!fs::exists(fixture.installDirectory() / L"_Delete"),
        "successful retry left the deletion quarantine behind");
  fixture.CheckSentinelsAndNoStageLitter();
}

void TestHybridClientDeletionShape() {
  Fixture fixture(L"hybrid-client-delete");
  const fs::path clientRoot =
      fixture.installDirectory() / L"Sites" / L"HybridDelete";
  const fs::path legacyProfile = clientRoot / L"Default";
  Check(fs::create_directories(legacyProfile),
        "could not create hybrid legacy profile fixture");
  WriteBytes(legacyProfile / L"Preferences", "legacy-profile-payload");
  WriteBytes(clientRoot / kLegacyBrowserBindingMarkerName,
             GetLegacyBrowserBindingText(BrowserKind::Edge));
  const client_activity::Record oldActivity{
      client_activity::ThreeMonthsBefore(client_activity::Now()) - 1, false};
  Check(client_activity::Write(clientRoot, oldActivity),
        "could not write hybrid activity fixture");
  std::wstring outcome;
  Check(DeleteEntireClient(L"HybridDelete", true, oldActivity, outcome),
        "recognized hybrid client deletion failed");
  Check(!fs::exists(clientRoot),
        "hybrid client deletion left its live root behind");
  Check(!fs::exists(fixture.installDirectory() / L"_Delete"),
        "hybrid client deletion left the quarantine behind");
  fixture.CheckSentinelsAndNoStageLitter();
}

void TestFirefoxClientDeletionShape() {
  Fixture fixture(L"firefox-client-delete");
  const fs::path clientRoot =
      fixture.installDirectory() / L"Sites" / L"FirefoxDelete";
  Check(fs::create_directory(clientRoot),
        "could not create Firefox v2 client fixture");
  WriteBytes(clientRoot / kClientSchemaMarkerName, kClientSchemaMarkerText);
  const fs::path slotRoot = clientRoot / L"Browsers" / L"Firefox";
  const fs::path profileRoot = slotRoot / L"Profile";
  Check(fs::create_directories(profileRoot),
        "could not create Firefox browser slot fixture");
  WriteBytes(slotRoot / kBrowserSchemaMarkerName,
             GetBrowserMarkerText(BrowserKind::Firefox));
  WriteBytes(profileRoot / L"prefs.js", "user_pref(\"fixture\", true);\n");
  std::wstring outcome;
  Check(DeleteEntireClient(L"FirefoxDelete", true, std::nullopt, outcome),
        "recognized Firefox client deletion failed");
  Check(!fs::exists(clientRoot),
        "Firefox client deletion left its live root behind");
  Check(!fs::exists(fixture.installDirectory() / L"_Delete"),
        "Firefox client deletion left the quarantine behind");
  fixture.CheckSentinelsAndNoStageLitter();
}

} // namespace

void CtSpacesInstallerTestAfterClientDeletePreflight(
    const fs::path &deleteRoot) {
  if (!g_armLateClientDeleteLock)
    return;
  Check(!g_lateClientDeleteLockPath.empty() &&
            IsStrictChildPath(g_lateClientDeleteLockPath, deleteRoot),
        "late client-delete lock target escaped the validated root");
  g_lateClientDeleteLock =
      ::CreateFileW(g_lateClientDeleteLockPath.c_str(), GENERIC_READ,
                    FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
  Check(g_lateClientDeleteLock != INVALID_HANDLE_VALUE,
        "post-preflight hook could not acquire the late deletion lock");
  g_armLateClientDeleteLock = false;
}

int CtSpacesInstallerTestMessageBox(HWND, LPCWSTR text, LPCWSTR title,
                                    UINT flags) {
  g_messages.push_back(
      {text ? text : L"", title ? title : L"", flags});
  if (g_handoffMode) {
    std::ofstream log(g_handoffLocalAppData / L"handoff-messages.txt",
                      std::ios::app);
    log << "message\n";
  }
  if ((flags & MB_YESNO) != 0) {
    if (g_nextAnswer >= g_answers.size())
      Fail("production installer asked an unexpected yes/no question");
    return g_answers[g_nextAnswer++];
  }
  return IDOK;
}

bool CtSpacesInstallerTestTryGetExeVersion(const fs::path &filePath,
                                           std::wstring &version) {
  if (!g_overrideVersions)
    return false;
  if (SamePath(filePath, g_currentExecutable)) {
    version = g_currentVersion;
    return true;
  }
  if (SamePath(filePath, g_expectedInstallDirectory / L"ctSpaces.exe")) {
    version = g_installedVersion;
    return true;
  }
  Fail("production update flow requested a version for an unexpected path");
}

namespace installer_test_boundary {

BOOL WINAPI ShellExecuteExW(SHELLEXECUTEINFOW *executeInfo) {
  ++g_launchCalls;
  Check(executeInfo != nullptr, "ShellExecuteExW received null details");
  Check(executeInfo->lpFile != nullptr &&
            SamePath(executeInfo->lpFile,
                     g_expectedInstallDirectory / L"ctSpaces.exe"),
        "launch escaped the fixture installed executable");
  Check(executeInfo->lpDirectory != nullptr &&
            SamePath(executeInfo->lpDirectory, g_expectedInstallDirectory),
        "launch escaped the fixture working directory");
  const std::wstring parameters =
      executeInfo->lpParameters ? executeInfo->lpParameters : L"";
  Check(parameters.starts_with(L"--wait-for-pid="),
        "launch omitted the parent-process wait argument");
  Check(parameters.find(L"--qa") == std::wstring::npos,
        "production relaunch unexpectedly propagated a QA argument");
  if (g_handoffMode) {
    const BOOL launched = ::ShellExecuteExW(executeInfo);
    if (launched && executeInfo->hProcess) {
      const fs::path pidPath = g_handoffLocalAppData / L"handoff-child.pid";
      std::ofstream pidFile(pidPath, std::ios::trunc);
      pidFile << GetProcessId(executeInfo->hProcess);
      pidFile.flush();
    }
    return launched;
  }
  if (!g_launchShouldSucceed) {
    executeInfo->hProcess = nullptr;
    SetLastError(ERROR_FILE_NOT_FOUND);
    return FALSE;
  }
  if (g_launchUseSuspendedProbeChild) {
    std::wstring commandLine =
        L"\"" + g_currentExecutable.wstring() +
        L"\" --installer-launch-probe-child";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    Check(::CreateProcessW(g_currentExecutable.c_str(), commandLine.data(),
                           nullptr, nullptr, FALSE,
                           CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr,
                           g_expectedInstallDirectory.c_str(), &startup,
                           &process) != FALSE,
          "could not create isolated suspended launch-probe child");
    HANDLE returnedProcess = nullptr;
    Check(DuplicateHandle(GetCurrentProcess(), process.hProcess,
                          GetCurrentProcess(), &returnedProcess, 0, FALSE,
                          DUPLICATE_SAME_ACCESS) != FALSE,
          "could not duplicate launch-probe process handle");
    g_probeProcess = process.hProcess;
    g_probeThread = process.hThread;
    executeInfo->hProcess = returnedProcess;
    return TRUE;
  }
  executeInfo->hProcess = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  Check(executeInfo->hProcess != nullptr,
        "could not create inert process-handle substitute");
  return TRUE;
}

LSTATUS WINAPI RegOpenKeyExW(HKEY key, LPCWSTR subKey, DWORD options,
                             REGSAM desiredAccess, PHKEY result) {
  ++g_registryReads;
  // The handoff child may read browser-discovery keys, but startup ownership
  // remains isolated by making the real per-user Run value appear absent.
  if (g_handoffMode) {
    static constexpr wchar_t runKey[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (!subKey || _wcsicmp(subKey, runKey) != 0) {
      return ::RegOpenKeyExW(key, subKey, options, desiredAccess, result);
    }
  }
  if (result)
    *result = nullptr;
  return ERROR_FILE_NOT_FOUND;
}

LSTATUS WINAPI RegCreateKeyExW(HKEY, LPCWSTR, DWORD, LPWSTR, DWORD, REGSAM,
                               const LPSECURITY_ATTRIBUTES, PHKEY result,
                               LPDWORD disposition) {
  ++g_forbiddenRegistryWrites;
  if (g_handoffMode)
    WriteBytes(g_handoffLocalAppData / L"registry-write-attempted", "create");
  if (result)
    *result = nullptr;
  if (disposition)
    *disposition = 0;
  return ERROR_ACCESS_DENIED;
}

LSTATUS WINAPI RegSetValueExW(HKEY, LPCWSTR, DWORD, DWORD, const BYTE *,
                              DWORD) {
  ++g_forbiddenRegistryWrites;
  if (g_handoffMode)
    WriteBytes(g_handoffLocalAppData / L"registry-write-attempted", "set");
  return ERROR_ACCESS_DENIED;
}

LSTATUS WINAPI RegDeleteValueW(HKEY, LPCWSTR) {
  ++g_forbiddenRegistryWrites;
  if (g_handoffMode)
    WriteBytes(g_handoffLocalAppData / L"registry-write-attempted", "delete");
  return ERROR_ACCESS_DENIED;
}

HRESULT WINAPI SHGetKnownFolderPath(REFKNOWNFOLDERID folderId, DWORD flags,
                                    HANDLE token, PWSTR *path) {
  ++g_knownFolderCalls;
  if (g_handoffMode && IsEqualGUID(folderId, FOLDERID_LocalAppData)) {
    if (!path)
      return E_POINTER;
    const size_t bytes =
        (g_handoffLocalAppData.wstring().size() + 1) * sizeof(wchar_t);
    auto *allocated = static_cast<PWSTR>(CoTaskMemAlloc(bytes));
    if (!allocated)
      return E_OUTOFMEMORY;
    memcpy(allocated, g_handoffLocalAppData.c_str(), bytes);
    *path = allocated;
    return S_OK;
  }
  if (g_handoffMode &&
      !IsEqualGUID(folderId, FOLDERID_Programs) &&
      !IsEqualGUID(folderId, FOLDERID_Desktop)) {
    return ::SHGetKnownFolderPath(folderId, flags, token, path);
  }
  if (path)
    *path = nullptr;
  return E_ACCESSDENIED;
}

HANDLE WINAPI CreateMutexW(LPSECURITY_ATTRIBUTES attributes,
                           BOOL initialOwner, LPCWSTR name) {
  static constexpr wchar_t productionMutex[] =
      L"Local\\{E19C159D-62C3-4412-A0A3-1A55A67C8C56}";
  if (g_handoffMode && name && wcscmp(name, productionMutex) == 0) {
    const std::wstring isolatedName =
        std::wstring(productionMutex) + L"-INSTALLER-" + g_handoffToken;
    return ::CreateMutexW(attributes, initialOwner, isolatedName.c_str());
  }
  return ::CreateMutexW(attributes, initialOwner, name);
}

HWND WINAPI FindWindowW(LPCWSTR className, LPCWSTR windowName) {
  if (g_handoffMode && className &&
      wcscmp(className, GUI_CLASS_NAME.c_str()) == 0) {
    return nullptr;
  }
  return ::FindWindowW(className, windowName);
}

HANDLE WINAPI CreateFileW(LPCWSTR fileName, DWORD desiredAccess,
                          DWORD shareMode,
                          LPSECURITY_ATTRIBUTES securityAttributes,
                          DWORD creationDisposition, DWORD flagsAndAttributes,
                          HANDLE templateFile) {
  if (g_failStageCreation && creationDisposition == CREATE_NEW && fileName) {
    const fs::path candidate(fileName);
    if (SamePath(candidate.parent_path(), g_expectedInstallDirectory) &&
        !SamePath(candidate,
                  g_expectedInstallDirectory / L"ctSpaces.exe")) {
      SetLastError(ERROR_ACCESS_DENIED);
      return INVALID_HANDLE_VALUE;
    }
  }
  return ::CreateFileW(fileName, desiredAccess, shareMode, securityAttributes,
                       creationDisposition, flagsAndAttributes, templateFile);
}

} // namespace installer_test_boundary

int wmain(int argc, wchar_t **argv) {
  try {
    if (argc == 2 &&
        wcscmp(argv[1], L"--installer-launch-probe-child") == 0) {
      return 0;
    }
    if (argc == 2 &&
        wcscmp(argv[1], L"--installer-handoff-updater") == 0) {
      Check(LoadHandoffEnvironment(),
            "updater handoff environment was not safely constrained");
      ResetBoundaries({IDYES, IDNO, IDNO, IDNO});
      g_handoffMode = true;
      g_expectedInstallDirectory =
          g_handoffLocalAppData / L"InfinitySys" / L"ctSpaces";
      return CtSpacesProductionWinMain(GetModuleHandleW(nullptr), nullptr,
                                       const_cast<LPWSTR>(L""), SW_SHOWNORMAL);
    }
    if (argc == 2 && wcsncmp(argv[1], L"--wait-for-pid=", 15) == 0 &&
        LoadHandoffEnvironment()) {
      return CtSpacesProductionWinMain(GetModuleHandleW(nullptr), nullptr,
                                       argv[1], SW_SHOWNORMAL);
    }
    const bool headlessOnly =
        argc == 2 &&
        wcscmp(argv[1], L"--installer-runtime-headless") == 0;
    g_currentExecutable = GetCurrentExecutablePath();
    Check(!g_currentExecutable.empty(),
          "could not resolve installer harness executable path");
    Check(IsSafeExistingRegularFile(g_currentExecutable),
          "installer harness is not a safe regular file");

    TestRealVersionResourceReader();
    TestFreshInstallSuccess();
    TestInstallDeclines();
    TestNewerUpdateSuccess();
    TestNewerUpdateDeclines();
    TestEqualVersionChoices();
    TestOlderVersionChoices();
    TestStagingFailurePreservesState();
    TestLockedDestinationPreservesState();
    TestLaunchFailureIsExplicit();
    TestLaunchUsesARealLiveProcessHandle();
    TestAlreadyInstalledPath();
    TestDefaultCloseKeepsConcurrentLaunchBusy();
    TestLockedClientDeleteCanBeRetried();
    TestHybridClientDeletionShape();
    TestFirefoxClientDeletionShape();
    if (!headlessOnly)
      TestRealInstalledChildReadinessHandoff();

    std::wcout << L"Installer runtime tests passed ("
               << (headlessOnly ? 20 : 21)
               << L" scenarios): real production "
                  L"doInstall/chkUpdate/copy/launch validation paths; all "
                  L"profile, registry, known-folder, dialog, and launch "
                  L"effects remained fixture-bound or intercepted.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Installer runtime test failed: " << error.what() << '\n';
    return 1;
  }
}
