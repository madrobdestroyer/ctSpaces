#include "../InProc7z.h"
#include "../ClientDeletionSafety.h"
#include "../TransientProfileIdentity.h"
#include "../resource.h"
#include "../version.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
namespace {
fs::path g_sDataDir, g_fixture, g_workRoot, g_templateArchive;
HWND g_hGui = nullptr;
bool g_bDefaultProfileUiBusy = false;
bool g_bUiEnabled = true;
unsigned g_checks = 0, g_sanitizerCalls = 0;
unsigned g_fixtureNumber = 0;
enum class SaveCase { Collision, LockedHistory, SanitizerFailure, InvalidOutput,
                      PublicationFailure, WorkRootSwap, WorkParentSwap };
SaveCase g_case = SaveCase::Collision;
enum class CopyFault { None, Write, Flush, Publish, Cleanup };
CopyFault g_copyFault = CopyFault::None;
HANDLE g_copyHandle = INVALID_HANDLE_VALUE;
fs::path g_copyStage;
bool g_copyFaultHit = false;
std::shared_ptr<transient_profile::Identity> g_expectedDirectory;
bool g_stageSwapAttempted = false, g_stageSwapSucceeded = false;
DWORD g_stageSwapError = ERROR_SUCCESS;
fs::path g_foreignAt;
std::vector<std::wstring> g_messages;
const std::wstring kRepeatedClock = L"2026-10-15_120000";
const std::string kSanitizedPreferences =
    "{\"browser\":{\"check_default_browser\":false,\"show_prompt_before_closing_tabs\":true,"
    "\"show_toolbar_bookmarks_button\":true},\"distribution\":{\"import_bookmarks\":false,"
    "\"import_history\":false,\"import_search_engine\":false,\"make_chrome_default_for_user\":false,"
    "\"skip_first_run_ui\":true},\"extensions\":{},\"profile\":{\"avatar_index\":0,"
    "\"exit_type\":\"Normal\",\"exited_cleanly\":true,\"name\":\"Default\","
    "\"using_default_avatar\":true,\"using_gaia_avatar\":false},"
    "\"intl\":{\"selected_languages\":\"en-GB\"}}";

void Check(bool value, const char *label) {
  ++g_checks;
  if (!value) throw std::runtime_error(label);
}
void Write(const fs::path &path, const std::string &value) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(value.data(), value.size());
  if (!stream) throw std::runtime_error("fixture write failed");
}
std::vector<char> Read(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("fixture read failed");
  return {std::istreambuf_iterator<char>(stream), {}};
}
std::wstring AnsiToWide(const std::string &value) {
  return std::wstring(value.begin(), value.end());
}
void SetUiState(bool enabled) { g_bUiEnabled = enabled && !g_bDefaultProfileUiBusy; }
enum class ProfileUseState { NotInUse, InUse, Indeterminate };
ProfileUseState ProbeExactBrowserProfileInUse(const fs::path &path, std::wstring &) {
  Check(path == g_sDataDir / L"Default", "profile probe escaped private fixture");
  return ProfileUseState::NotInUse;
}
int FixtureMessage(HWND, LPCWSTR text, LPCWSTR title, UINT) {
  g_messages.emplace_back(text ? text : L"");
  return title && std::wstring_view(title) == L"Save Default Profile" ? IDYES : IDOK;
}
void ProgressUI_Show(HWND, const std::wstring &, int) {}
void ProgressUI_Hide() {}
bool ExtractResourceToFile(int resource, const fs::path &path, bool) {
  Check(resource == IDR_DEFAULT_PROFILE_SANITIZER &&
        path.parent_path().parent_path() == g_sDataDir / L"_DefaultSave",
        "resource boundary escaped Default-save fixture");
  g_workRoot = path.parent_path();
  Write(path, "# Inert fixture sanitizer boundary; never executed\n");
  return true;
}
std::wstring QuoteCommandLineArgument(const std::wstring &value) { return L"\"" + value + L"\""; }
struct HiddenCommandResult {
  bool launched = false, timedOut = false;
  DWORD exitCode = DWORD(-1), systemError = ERROR_SUCCESS;
};
HiddenCommandResult RunHiddenCommandAndWait(const fs::path &, const std::wstring &, DWORD) {
  ++g_sanitizerCalls;
  if (g_case == SaveCase::WorkRootSwap || g_case == SaveCase::WorkParentSwap) {
    g_stageSwapAttempted = true;
    const auto selected = g_case == SaveCase::WorkRootSwap
        ? g_workRoot : g_workRoot.parent_path();
    const auto preserved = g_sDataDir / L"original-default-save-stage";
    g_stageSwapSucceeded = MoveFileExW(selected.c_str(), preserved.c_str(), 0) != FALSE;
    g_stageSwapError = g_stageSwapSucceeded ? ERROR_SUCCESS : GetLastError();
    if (g_stageSwapSucceeded) {
      fs::create_directories(g_workRoot.parent_path());
      Check(MoveFileExW((g_sDataDir / L"Sites/Keep").c_str(), g_workRoot.c_str(), 0) != FALSE,
            "private independent client could not substitute the unguarded staging name");
      g_foreignAt = g_workRoot;
    }
    return {true, false, 37, ERROR_SUCCESS};
  }
  if (g_case == SaveCase::SanitizerFailure)
    return {true, false, 37, ERROR_SUCCESS};
  const auto sanitized = g_workRoot / L"Sanitized";
  Check(SUCCEEDED(_7zExtra_7z(g_templateArchive.c_str(), sanitized.c_str(), nullptr, nullptr)),
        "actual SDK could not prepare a previously sanitized starter fixture");
  Write(sanitized / L"Default/Preferences", kSanitizedPreferences);
  if (g_case == SaveCase::InvalidOutput)
    Write(sanitized / L"Default/unexpected-runtime.dat", "REJECTED SYNTHETIC OUTPUT");
  return {true, false, 0, ERROR_SUCCESS};
}
std::wstring GetBackupTimestamp() { return kRepeatedClock; }
bool RemoveTransientProfileIfIdleAndVerify(const fs::path &path, std::wstring &,
    const std::shared_ptr<transient_profile::Identity> &directory) {
  Check(path == g_sDataDir / L"Default" && g_sDataDir.parent_path() == g_fixture,
        "raw-profile privacy cleanup escaped private fixture");
  Check(directory && directory == g_expectedDirectory,
        "save did not carry the captured editor-directory lifetime to cleanup");
  directory->Release();
  std::error_code error;
  fs::remove_all(path, error);
  return !error && !fs::exists(path);
}

HANDLE WINAPI FixtureCopyCreate(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
                                DWORD, DWORD, HANDLE);
BOOL WINAPI FixtureCopyWrite(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
BOOL WINAPI FixtureCopyFlush(HANDLE);
BOOL WINAPI FixtureCopyInformation(HANDLE, FILE_INFO_BY_HANDLE_CLASS, LPVOID, DWORD);

// Generated directly from the reviewed source at build time. Dialog, progress,
// process probe, clock and sanitizer execution are the explicit inert boundaries
// above. Filesystem save control flow, exact policy validation and SDK calls are
// unchanged production function bodies.
#define MessageBoxW FixtureMessage
#define CreateFileW FixtureCopyCreate
#define WriteFile FixtureCopyWrite
#define FlushFileBuffers FixtureCopyFlush
#define SetFileInformationByHandle FixtureCopyInformation
#include "DefaultSaveTransactionSource.inl"
#undef SetFileInformationByHandle
#undef FlushFileBuffers
#undef WriteFile
#undef CreateFileW
#undef MessageBoxW

HANDLE WINAPI FixtureCopyCreate(LPCWSTR name, DWORD access, DWORD sharing,
    LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD flags, HANDLE templ) {
  HANDLE file = ::CreateFileW(name, access, sharing, security, creation, flags, templ);
  if (file != INVALID_HANDLE_VALUE && creation == CREATE_NEW &&
      (access & (GENERIC_WRITE | DELETE)) == (GENERIC_WRITE | DELETE)) {
    g_copyHandle = file;
    g_copyStage = name;
  }
  return file;
}
BOOL WINAPI FixtureCopyWrite(HANDLE file, LPCVOID bytes, DWORD count,
                            LPDWORD written, LPOVERLAPPED overlapped) {
  if (file == g_copyHandle && !g_copyFaultHit &&
      (g_copyFault == CopyFault::Write || g_copyFault == CopyFault::Cleanup)) {
    const DWORD partial = (std::min)(count, DWORD{17});
    if (partial && !::WriteFile(file, bytes, partial, written, overlapped)) return FALSE;
    g_copyFaultHit = true;
    SetLastError(ERROR_DISK_FULL);
    return FALSE;
  }
  return ::WriteFile(file, bytes, count, written, overlapped);
}
BOOL WINAPI FixtureCopyFlush(HANDLE file) {
  if (file == g_copyHandle && g_copyFault == CopyFault::Flush) {
    g_copyFaultHit = true;
    SetLastError(ERROR_WRITE_FAULT);
    return FALSE;
  }
  return ::FlushFileBuffers(file);
}
BOOL WINAPI FixtureCopyInformation(HANDLE file, FILE_INFO_BY_HANDLE_CLASS kind,
                                   LPVOID info, DWORD bytes) {
  if (file == g_copyHandle && kind == FileRenameInfo && g_copyFault == CopyFault::Publish) {
    g_copyFaultHit = true;
    SetLastError(ERROR_DISK_FULL);
    return FALSE;
  }
  if (file == g_copyHandle && kind == FileDispositionInfo && g_copyFault == CopyFault::Cleanup) {
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
  }
  return ::SetFileInformationByHandle(file, kind, info, bytes);
}

void TestSaveCase(SaveCase testCase, bool observationOnly) {
  g_case = testCase;
  g_messages.clear(); g_sanitizerCalls = 0;
  g_fixture = fs::current_path() / L"build" /
      std::format(L"ds8-{}-{}-{}", GetCurrentProcessId(), GetTickCount64(), ++g_fixtureNumber);
  Check(fs::create_directory(g_fixture), "private Default-save fixture creation failed");
  g_sDataDir = g_fixture / L"d";
  fs::create_directories(g_sDataDir / L"Default/Default");
  fs::create_directories(g_sDataDir / L"Sites/Keep");
  fs::create_directories(g_sDataDir / L"_DefBak");
  Write(g_sDataDir / L"Default/private-state.bin", "PRIVATE DISPOSABLE EDITOR STATE");
  Write(g_sDataDir / L"Sites/Keep/notes.bin", "EXACT UNRELATED CLIENT");
  const auto clientBefore = Read(g_sDataDir / L"Sites/Keep/notes.bin");
  const auto starter = g_sDataDir / L"Default.7z";
  const auto history = g_sDataDir / L"_DefBak" / (kRepeatedClock + L"-Sanitized-Default.7z");
  fs::copy_file(g_templateArchive, starter);
  fs::copy_file(g_templateArchive, history);
  const auto prior = Read(history), oldStarter = Read(starter);
  Check(SUCCEEDED(_7zTest7z(history.c_str(), nullptr, nullptr)),
        "existing recovery history fixture must be a real valid archive");
  HANDLE locked = INVALID_HANDLE_VALUE;
  if (testCase == SaveCase::LockedHistory || testCase == SaveCase::PublicationFailure) {
    locked = CreateFileW((testCase == SaveCase::LockedHistory ? history : starter).c_str(),
        GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "native history/publication lock could not be acquired");
  }
  g_expectedDirectory = transient_profile::Identity::Capture(g_sDataDir / L"Default");
  Check(g_expectedDirectory != nullptr, "private editor-directory lifetime capture failed");
  HandleClosedDefaultProfile(g_expectedDirectory);
  g_expectedDirectory.reset();
  if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
  const bool kept = Read(history) == prior;
  const bool changed = Read(starter) != oldStarter;
  const bool removed = !fs::exists(g_sDataDir / L"Default");
  std::cout << "CASE=" << static_cast<unsigned>(testCase)
      << " SAVED=" << (g_sanitizerCalls == 1 && changed)
      << " HISTORY_KEPT=" << kept << " STARTER_CHANGED=" << changed
      << " RAW_REMOVED=" << removed << '\n';
  std::wcout << L"FIXTURE=" << g_fixture << L'\n';
  for (const auto &message : g_messages) std::wcout << L"MESSAGE=" << message << L'\n';
  Check(Read(g_sDataDir / L"Sites/Keep/notes.bin") == clientBefore,
        "Default save changed an unrelated client");
  Check(removed, "documented raw-data deletion privacy behavior changed");
  Check(g_bUiEnabled && !g_bDefaultProfileUiBusy, "Default save left its busy state stuck");
  Check(SUCCEEDED(_7zTest7z(starter.c_str(), nullptr, nullptr)), "starter is not a valid SDK archive");
  if (testCase == SaveCase::SanitizerFailure || testCase == SaveCase::InvalidOutput ||
      testCase == SaveCase::PublicationFailure) {
    Check(!changed && kept, "failed save changed the old starter or existing recovery archive");
    Check(g_sanitizerCalls == 1 && std::ranges::none_of(g_messages, [](const auto &message) {
          return message.find(L"The Default profile was saved.") != std::wstring::npos;
        }), "failed save reported success or bypassed sanitizer boundary");
    Check(std::ranges::any_of(g_messages, [](const auto &message) {
          return message.find(L"previous starter is unchanged") != std::wstring::npos;
        }), "failed save omitted its previous-starter preservation explanation");
    for (const auto &entry : fs::directory_iterator(g_sDataDir / L"_DefBak"))
      Check(entry.is_regular_file() && SUCCEEDED(_7zTest7z(entry.path().c_str(), nullptr, nullptr)),
            "failed save left invalid bytes in recovery history");
    Check(!fs::exists(g_sDataDir / L"_DefaultSave"), "failed save left an unlocked owned work directory");
    return;
  }
  Check(g_sanitizerCalls == 1 && changed &&
        std::ranges::any_of(g_messages, [](const auto &message) {
          return message.find(L"The Default profile was saved.") != std::wstring::npos;
        }), "collision case did not successfully publish the changed starter");
  const auto verified = g_fixture / L"verify";
  Check(SUCCEEDED(_7zExtra_7z(starter.c_str(), verified.c_str(), nullptr, nullptr)),
        "saved starter could not be extracted through actual SDK");
  Check(Read(verified / L"Default/Preferences") ==
        std::vector<char>(kSanitizedPreferences.begin(), kSanitizedPreferences.end()),
        "saved starter does not contain the intended changed allowlisted preferences");
  if (!observationOnly) {
    Check(kept, "save overwrote a retained recovery archive with the same clock name");
    Check(std::distance(fs::directory_iterator(g_sDataDir / L"_DefBak"),
                        fs::directory_iterator{}) == 2,
          "successful save did not retain both distinct recovery archives");
  }
}

void TestRecoveryCopyFault(CopyFault fault) {
  const auto fixture = fs::current_path() / L"build" /
      std::format(L"dsc8-{}-{}-{}", GetCurrentProcessId(), GetTickCount64(), ++g_fixtureNumber);
  Check(fs::create_directory(fixture), "direct recovery-copy fixture creation failed");
  const auto backupDir = fixture / L"_DefBak", source = fixture / L"source.7z";
  fs::create_directory(backupDir);
  fs::copy_file(g_templateArchive, source);
  const auto history = backupDir / (kRepeatedClock + L"-Sanitized-Default.7z");
  fs::copy_file(g_templateArchive, history);
  const auto originalHistory = Read(history), originalSource = Read(source);
  g_copyFault = fault; g_copyFaultHit = false;
  g_copyHandle = INVALID_HANDLE_VALUE; g_copyStage.clear();
  std::wstring details;
  const bool copied = CopyDefaultRecoveryArchive(source, backupDir, details);
  std::wcout << L"COPY_FAULT=" << static_cast<unsigned>(fault)
      << L" HIT=" << g_copyFaultHit << L" OK=" << copied << L" DETAILS=" << details << L'\n';
  Check(g_copyFaultHit && !copied, "requested recovery native failure was not rejected");
  Check(Read(history) == originalHistory && Read(source) == originalSource,
        "rejected recovery copy changed its prior history or source bytes");
  Check(!details.empty(), "recovery copy failure omitted error details");
  if (fault == CopyFault::Cleanup) {
    const auto partial = Read(g_copyStage);
    Check(partial.size() == 17 && std::equal(partial.begin(), partial.end(), originalSource.begin()),
          "cleanup refusal did not retain the exact owned partial bytes");
    Check(details.find(g_copyStage.wstring()) != std::wstring::npos,
          "cleanup refusal omitted its exact retained partial path");
  } else {
    Check(!fs::exists(g_copyStage) &&
          std::distance(fs::directory_iterator(backupDir), fs::directory_iterator{}) == 1,
          "failed recovery copy left an owned incomplete staging artifact");
  }
  g_copyFault = CopyFault::None;
  Check(CopyDefaultRecoveryArchive(source, backupDir, details),
        "recovery copy did not recover after native fault removal");
  Check(Read(history) == originalHistory && Read(source) == originalSource,
        "recovery retry changed original history or source");
  unsigned validCopies = 0;
  for (const auto &entry : fs::directory_iterator(backupDir)) {
    if (entry.path().extension() == L".7z") {
      Check(SUCCEEDED(_7zTest7z(entry.path().c_str(), nullptr, nullptr)),
            "published recovery history failed real SDK CRC testing");
      ++validCopies;
    }
  }
  Check(validCopies == 2, "recovery retry did not retain old and new valid archives");
}

void TestStagingNamespaceFailure(SaveCase testCase) {
  g_case = testCase; g_messages.clear(); g_sanitizerCalls = 0;
  g_stageSwapAttempted = g_stageSwapSucceeded = false;
  g_stageSwapError = ERROR_SUCCESS;
  g_fixture = fs::current_path() / L"build/app-audit-round12-2026-10-15/clients" /
      std::format(L"default-stage-{}-{}-{}", GetCurrentProcessId(), GetTickCount64(), ++g_fixtureNumber);
  Check(fs::create_directory(g_fixture), "private staging-namespace fixture creation failed");
  g_sDataDir = g_fixture / L"d";
  fs::create_directories(g_sDataDir / L"Default/Default");
  fs::create_directories(g_sDataDir / L"Sites/Keep");
  Write(g_sDataDir / L"Default/private-state.bin", "PRIVATE DISPOSABLE EDITOR STATE");
  Write(g_sDataDir / L"Sites/Keep/notes.bin", "EXACT UNRELATED CLIENT");
  g_foreignAt = g_sDataDir / L"Sites/Keep";
  fs::copy_file(g_templateArchive, g_sDataDir / L"Default.7z");
  const auto originalStarter = Read(g_sDataDir / L"Default.7z");
  g_expectedDirectory = transient_profile::Identity::Capture(g_sDataDir / L"Default");
  Check(g_expectedDirectory != nullptr, "staging fixture captures exact editor identity");
  HandleClosedDefaultProfile(g_expectedDirectory);
  g_expectedDirectory.reset();
  const bool kept = fs::exists(g_foreignAt / L"notes.bin") &&
      Read(g_foreignAt / L"notes.bin") == std::vector<char>({'E','X','A','C','T',' ','U','N','R','E','L','A','T','E','D',' ','C','L','I','E','N','T'});
  std::cout << "DEFAULT_STAGE_SWAP parent=" << (testCase == SaveCase::WorkParentSwap)
      << " attempted=" << g_stageSwapAttempted << " substituted=" << g_stageSwapSucceeded
      << " error=" << g_stageSwapError << " FOREIGN_DATA_LOST=" << !kept << '\n';
  std::wcout << L"FIXTURE=" << g_fixture << L'\n';
  Check(g_stageSwapAttempted && g_sanitizerCalls == 1,
        "stage substitution reaches the real failed-sanitizer cleanup boundary");
  Check(kept, "Default-save failure cleanup must never remove an independent substituted client");
  Check(!g_stageSwapSucceeded && (g_stageSwapError == ERROR_SHARING_VIOLATION ||
                                 g_stageSwapError == ERROR_ACCESS_DENIED),
        "original work root and ancestor remain pinned through the sanitizer boundary");
  Check(Read(g_sDataDir / L"Default.7z") == originalStarter,
        "rejected staging namespace mutation preserves exact previous starter bytes");
  Check(!fs::exists(g_sDataDir / L"Default") && g_bUiEnabled && !g_bDefaultProfileUiBusy,
        "failed guarded stage retains raw-profile privacy cleanup and releases UI busy state");
  Check(!fs::exists(g_sDataDir / L"_DefaultSave"),
        "failed owned staging tree is cleaned after namespace guards are safely released");
}
} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    const bool observe = argc == 2 && wcscmp(argv[1], L"--before-proof") == 0;
    const bool stageOnly = argc == 2 && wcscmp(argv[1], L"--stage-namespace-only") == 0;
    Check(argc == 1 || observe || stageOnly, "unknown arguments refused");
    g_templateArchive = fs::current_path() / L"Default.7z";
    _7zSetHInstance(GetModuleHandleW(nullptr));
    if (stageOnly) {
      TestStagingNamespaceFailure(SaveCase::WorkRootSwap);
      TestStagingNamespaceFailure(SaveCase::WorkParentSwap);
      std::cout << "Default staging namespace checks passed: " << g_checks << " (2 scenarios).\n";
      return 0;
    }
    TestSaveCase(SaveCase::Collision, observe);
    if (!observe) {
      TestSaveCase(SaveCase::LockedHistory, false);
      TestSaveCase(SaveCase::SanitizerFailure, false);
      TestSaveCase(SaveCase::InvalidOutput, false);
      TestSaveCase(SaveCase::PublicationFailure, false);
      for (const auto fault : {CopyFault::Write, CopyFault::Flush, CopyFault::Publish, CopyFault::Cleanup})
        TestRecoveryCopyFault(fault);
      TestStagingNamespaceFailure(SaveCase::WorkRootSwap);
      TestStagingNamespaceFailure(SaveCase::WorkParentSwap);
    }
    std::cout << "Default save transaction checks passed: " << g_checks
              << " (" << (observe ? 1 : 11) << " scenarios).\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Default save transaction check failed: " << error.what() << '\n';
    return 1;
  }
}
