// Production-linked legacy ZIP compatibility with small, ordinary owned archives.
#include <windows.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef CTSPACES_LEGACY_ZIP_FIXTURE_ROOT
#error Use Test-LegacyZipRestore.ps1 with a compile-bound fixture root.
#endif

namespace legacy_zip_test {
static BOOL WINAPI LaunchOwnedPowerShell(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES,
    LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW,
    LPPROCESS_INFORMATION);
}

#define CreateProcessW legacy_zip_test::LaunchOwnedPowerShell
#define wWinMain UnusedLegacyZipApplicationEntry
#include "../ctSpaces.cpp"
#undef wWinMain
#undef CreateProcessW

namespace legacy_zip_test {
using Tree = std::map<std::wstring, std::optional<std::string>>;
static fs::path fixture, systemPowerShell, expectedArchive, expectedStage;
static std::vector<HWINEVENTHOOK> observers;
static std::vector<DWORD> childPids;
static unsigned checks, childAttempts, childLaunches, forbidden, shows, focuses, foreground;
static bool permitExtraction;

static void Require(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}

static void NoReparseAncestors(const fs::path &path) {
  for (auto current = path; !current.empty();) {
    const DWORD attributes = GetFileAttributesW(current.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
      throw std::runtime_error("Reparse path refused in private legacy ZIP fixture");
    const auto parent = current.parent_path();
    if (parent == current) break;
    current = parent;
  }
}

static std::string ReadSmallFile(const fs::path &path) {
  NoReparseAncestors(path);
  if (!fs::is_regular_file(path) || fs::file_size(path) > 64 * 1024)
    throw std::runtime_error("Expected a bounded regular fixture file");
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Could not open private fixture file");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

static Tree Snapshot(const fs::path &root) {
  NoReparseAncestors(root);
  if (!fs::is_directory(root)) throw std::runtime_error("Missing fixture tree");
  Tree tree;
  for (const auto &entry : fs::recursive_directory_iterator(root)) {
    const auto path = entry.path();
    NoReparseAncestors(path);
    const auto name = path.lexically_relative(root).generic_wstring();
    if (entry.is_directory()) tree.emplace(name, std::nullopt);
    else tree.emplace(name, ReadSmallFile(path));
  }
  return tree;
}

static fs::path Seed(const wchar_t *name) {
  const auto root = fixture / name;
  Require(fs::create_directory(root), "Expected a fresh legacy ZIP case directory");
  Require(fs::create_directories(root / L"Sites/Previous Client"), "Could not create private previous collection");
  Require(fs::create_directory(root / L"stage"), "Could not create fresh staging directory");
  std::ofstream old(root / L"Sites/Previous Client/original.txt", std::ios::binary);
  old << "LEGACY ZIP TEST ORIGINAL COLLECTION\n";
  old.close();
  Require(static_cast<bool>(old), "Could not write private original marker");
  return root;
}

static void CALLBACK ObserveEvent(HWINEVENTHOOK, DWORD event, HWND, LONG object,
                                  LONG, DWORD, DWORD) {
  if (event == EVENT_OBJECT_SHOW && object == OBJID_WINDOW) ++shows;
  if (event == EVENT_OBJECT_FOCUS) ++focuses;
  if (event == EVENT_SYSTEM_FOREGROUND) ++foreground;
}

static bool ObserveProcess(DWORD pid) {
  for (const DWORD event : {EVENT_OBJECT_SHOW, EVENT_OBJECT_FOCUS, EVENT_SYSTEM_FOREGROUND}) {
    const auto hook = SetWinEventHook(event, event, nullptr, ObserveEvent, pid, 0, WINEVENT_OUTOFCONTEXT);
    if (!hook) return false;
    observers.push_back(hook);
  }
  return true;
}

static LRESULT CALLBACK VetoActivation(int code, WPARAM wParam, LPARAM lParam) {
  if (code == HCBT_ACTIVATE || code == HCBT_SETFOCUS) {
    ++forbidden;
    return 1;
  }
  return CallNextHookEx(nullptr, code, wParam, lParam);
}

static BOOL WINAPI LaunchOwnedPowerShell(LPCWSTR executable, LPWSTR command,
    LPSECURITY_ATTRIBUTES processSecurity, LPSECURITY_ATTRIBUTES threadSecurity,
    BOOL inherit, DWORD flags, LPVOID environment, LPCWSTR cwd,
    LPSTARTUPINFOW startup, LPPROCESS_INFORMATION process) {
  ++childAttempts;
  const std::wstring_view text(command ? command : L"");
  const auto prefix = L"\"" + systemPowerShell.wstring() + L"\" -NoLogo -NoProfile -NonInteractive ";
  const auto arguments = L";$archive=" + QuotePowerShellSingle(expectedArchive.wstring()) +
      L";$destination=" + QuotePowerShellSingle(expectedStage.wstring()) + L";$count=";
  if (!permitExtraction || !executable || fs::path(executable) != systemPowerShell ||
      !text.starts_with(prefix) || text.find(arguments) == std::wstring_view::npos ||
      processSecurity || threadSecurity || inherit || environment || cwd || !startup || !process ||
      flags != (CREATE_NO_WINDOW | CREATE_SUSPENDED) ||
      (startup->dwFlags & STARTF_USESHOWWINDOW) == 0 || startup->wShowWindow != SW_HIDE) {
    ++forbidden;
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
  }
  permitExtraction = false;
  if (!::CreateProcessW(executable, command, processSecurity, threadSecurity, inherit,
                        flags, environment, cwd, startup, process)) {
    const DWORD error = GetLastError();
    std::cout << "LEGACY_ZIP_CHILD_LAUNCH_ERROR=" << error << '\n';
    SetLastError(error);
    return FALSE;
  }
  if (!ObserveProcess(process->dwProcessId)) {
    const DWORD error = GetLastError();
    ::TerminateProcess(process->hProcess, ERROR_GEN_FAILURE);
    WaitForSingleObject(process->hProcess, 5000);
    CloseHandle(process->hThread);
    CloseHandle(process->hProcess);
    *process = {};
    ++forbidden;
    SetLastError(error ? error : ERROR_GEN_FAILURE);
    return FALSE;
  }
  ++childLaunches;
  childPids.push_back(process->dwProcessId);
  std::cout << "LEGACY_ZIP_HIDDEN_CHILD_PID=" << process->dwProcessId << '\n';
  return TRUE;
}

static CtBackup::Result Extract(const fs::path &archive, const fs::path &stage,
                               unsigned expectedChildren) {
  NoReparseAncestors(archive);
  NoReparseAncestors(stage);
  Require(archive.parent_path() == fixture && stage.parent_path().parent_path() == fixture &&
          stage.filename() == L"stage" && fs::is_empty(stage),
          "Legacy ZIP command inputs must be the exact archive and fresh stage under the private fixture");
  expectedArchive = archive;
  expectedStage = stage;
  permitExtraction = expectedChildren != 0;
  const auto previousAttempts = childAttempts;
  const auto previousLaunches = childLaunches;
  const auto result = ExtractLegacyZip(archive, stage);
  permitExtraction = false;
  std::cout << "LEGACY_ZIP_EXTRACT_HRESULT=0x" << std::hex
            << static_cast<unsigned long>(result.code) << std::dec << '\n';
  Require(childAttempts - previousAttempts == expectedChildren && childLaunches - previousLaunches == expectedChildren,
          "Legacy ZIP command did not use the expected number of genuine owned hidden children");
  return result;
}

static bool CountFinalGuard(void *context, std::wstring &) {
  ++*static_cast<unsigned *>(context);
  return true;
}

static void RequireLocalZipWriteTime(const fs::path &path, const SYSTEMTIME &expected) {
  WIN32_FILE_ATTRIBUTE_DATA attributes{};
  FILETIME local{};
  SYSTEMTIME actual{};
  Require(GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes) &&
          FileTimeToLocalFileTime(&attributes.ftLastWriteTime, &local) &&
          FileTimeToSystemTime(&local, &actual) &&
          actual.wYear == expected.wYear && actual.wMonth == expected.wMonth &&
          actual.wDay == expected.wDay && actual.wHour == expected.wHour &&
          actual.wMinute == expected.wMinute && actual.wSecond == expected.wSecond &&
          actual.wMilliseconds == 0, "The ZIP's local DOS write time survives exactly");
}

static void SuccessfulRestore(const char *label, const fs::path &root,
    const fs::path &archive, bool hasRootSites, unsigned profiles, const Tree &expected,
    const std::map<fs::path, SYSTEMTIME> &writeTimes = {}) {
  const auto stage = root / L"stage";
  const auto live = root / L"Sites";
  const auto before = Snapshot(live);
  const auto archiveBytes = ReadSmallFile(archive);
  Require(Extract(archive, stage, 1).ok(), "Production System32 extraction rejected an ordinary legacy ZIP");
  Require(fs::is_directory(stage / L"Sites") == hasRootSites, "ZIP extracted to its original legacy layout");
  CtBackup::StagedRestore staged;
  Require(NormalizeLegacyZipStaging(stage, staged).ok() && staged.validation,
          "Production normalization sealed the extracted legacy collection");
  Require(staged.profileCount == profiles && staged.payloadDir == stage / L"Sites" && Snapshot(staged.payloadDir) == expected,
          "Sealed legacy payload matches all expected files, directory entries, and clients");
  for (const auto &[path, expectedTime] : writeTimes)
    RequireLocalZipWriteTime(staged.payloadDir / path, expectedTime);
  Require(Snapshot(live) == before, "Extraction and normalization did not alter the active fixture collection");
  DWORD error = ERROR_SUCCESS;
  const auto recovery = UniqueRecoveryPath(root, L"2026-11-12_120000", &error);
  Require(error == ERROR_SUCCESS && recovery.parent_path() == root && recovery.filename().native().size() == 5 &&
          recovery.filename().native().front() == L'R' && !fs::exists(recovery),
          "Production chose a fresh compact recovery sibling");
  unsigned commitGuards = 0;
  Require(CtBackup::CommitStagedRestore(staged, live, recovery, CountFinalGuard, &commitGuards).ok(),
          "Production restore transaction committed the normalized ordinary ZIP");
  Require(commitGuards == 1 && Snapshot(live) == expected && Snapshot(recovery) == before && !fs::exists(stage),
          "Commit preserved the exact previous collection and installed the exact legacy payload");
  for (const auto &[path, expectedTime] : writeTimes)
    RequireLocalZipWriteTime(live / path, expectedTime);
  Require(ReadSmallFile(archive) == archiveBytes, "Original legacy ZIP bytes were unchanged");
  std::cout << "LEGACY_ZIP_CASE=" << label << " result=passed\n";
}

static void MixedLayoutRefusal() {
  const auto root = Seed(L"mixed");
  const auto archive = fixture / L"mixed.zip";
  const auto stage = root / L"stage";
  const auto before = Snapshot(root / L"Sites");
  const auto archiveBytes = ReadSmallFile(archive);
  Require(Extract(archive, stage, 1).ok(), "Ordinary mixed-layout ZIP is physically valid and extracted safely");
  const Tree expected{{L"Sites", std::nullopt}, {L"Sites/Alpha", std::nullopt},
                      {L"Sites/Alpha/content.txt", "MIXED CLIENT\n"}, {L"readme.txt", "ORDINARY ROOT README\n"}};
  Require(Snapshot(stage) == expected, "Mixed layout contains only the intended benign staged entries");
  CtBackup::StagedRestore staged;
  const auto result = NormalizeLegacyZipStaging(stage, staged);
  Require(result.code == E_INVALIDARG && result.details.find(L"unexpected root data") != std::wstring::npos &&
          !staged.validation && staged.stagingRoot.empty(), "Application refuses the unsupported collection shape before commit");
  Require(Snapshot(stage) == expected && Snapshot(root / L"Sites") == before && ReadSmallFile(archive) == archiveBytes &&
          std::distance(fs::directory_iterator(root), fs::directory_iterator{}) == 2,
          "Mixed-layout refusal preserved stage, live collection, and source ZIP without recovery moves");
  std::cout << "LEGACY_ZIP_CASE=mixed-layout result=passed\n";
}

static void MissingArchiveRefusal() {
  const auto root = Seed(L"missing");
  const auto before = Snapshot(root / L"Sites");
  const auto missing = fixture / L"missing.zip";
  Require(!fs::exists(missing), "Missing archive fixture path is absent");
  const auto result = Extract(missing, root / L"stage", 0);
  Require(result.code == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) && result.details.find(L"does not exist") != std::wstring::npos,
          "Missing legacy ZIP returns file not found before launching any extraction process");
  Require(Snapshot(root / L"Sites") == before && fs::is_empty(root / L"stage") &&
          std::distance(fs::directory_iterator(root), fs::directory_iterator{}) == 2,
          "Missing archive did not alter live data or create recovery");
  std::cout << "LEGACY_ZIP_CASE=missing result=passed\n";
}

static void SharingRefusalAndRetry(bool before, const Tree &expected) {
  const auto root = Seed(L"sharing");
  const auto archive = fixture / L"flat.zip";
  const auto stage = root / L"stage";
  const auto liveBefore = Snapshot(root / L"Sites");
  const auto archiveBytes = ReadSmallFile(archive);
  {
    const ScopedKernelHandle locked(CreateFileW(archive.c_str(), GENERIC_READ, 0, nullptr,
                                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    Require(static_cast<bool>(locked), "Could not acquire the exclusive read lease on the valid private ZIP");
    const ScopedKernelHandle probe(CreateFileW(archive.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    const auto sharingError = probe ? ERROR_SUCCESS : GetLastError();
    Require(!probe && sharingError == ERROR_SHARING_VIOLATION,
            "Independent Windows open did not establish a real archive sharing violation");
    const auto result = Extract(archive, stage, before ? 1 : 0);
    std::cout << "LEGACY_ZIP_SHARING_HRESULT=0x" << std::hex
              << static_cast<unsigned long>(result.code) << std::dec << '\n';
    if (before) {
      Require(result.code == E_FAIL &&
              result.details.find(L"Legacy ZIP validation or extraction failed (exit code ") != std::wstring::npos,
              "Expected the baseline to collapse the sharing violation into a generic child error");
      std::cout << "LEGACY_ZIP_SHARING_GENERIC_BEFORE=1\n";
    } else {
      Require(result.code == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION) &&
              result.details.find(L"in use") != std::wstring::npos,
              "Expected the repaired legacy ZIP path to report the archive as in use");
      std::cout << "LEGACY_ZIP_SHARING_IN_USE_AFTER=1\n";
    }
    Require(Snapshot(root / L"Sites") == liveBefore && fs::is_empty(stage) &&
            std::distance(fs::directory_iterator(root), fs::directory_iterator{}) == 2,
            "Legacy ZIP sharing refusal altered live data, staging or recovery");
  }
  Require(ReadSmallFile(archive) == archiveBytes, "Sharing refusal changed the ordinary ZIP bytes");
  std::cout << "LEGACY_ZIP_SHARING_PRESERVED=1\n";
  SuccessfulRestore("sharing-retry", root, archive, false, 1, expected);
  std::cout << "LEGACY_ZIP_CASE=sharing result=passed\n";
}

static void ApostropheFilenames(bool before, const Tree &expected) {
  const auto ascii = fixture / L"O'Brien backup.zip";
  const auto typographic = fixture / L"O\u2019Brien backup.zip";
  const auto archiveBytes = ReadSmallFile(typographic);
  Require(archiveBytes == ReadSmallFile(ascii) &&
          archiveBytes == ReadSmallFile(fixture / L"flat.zip"),
          "Apostrophe filenames refer to byte-identical copies of the ordinary flat ZIP");

  SuccessfulRestore("ascii-apostrophe", Seed(L"ascii-quote"), ascii, false, 1, expected);
  const auto root = Seed(L"smart-quote");
  if (!before) {
    SuccessfulRestore("smart-apostrophe", root, typographic, false, 1, expected);
    return;
  }

  const auto liveBefore = Snapshot(root / L"Sites");
  const auto stage = root / L"stage";
  const auto result = Extract(typographic, stage, 1);
  std::cout << "LEGACY_ZIP_SMART_APOSTROPHE_HRESULT=0x" << std::hex
            << static_cast<unsigned long>(result.code) << std::dec << '\n';
  Require(result.code == E_FAIL &&
          result.details.find(L"Legacy ZIP validation or extraction failed (exit code ") != std::wstring::npos,
          "The baseline hidden child fails to extract the ordinary ZIP under its typographic filename");
  Require(Snapshot(root / L"Sites") == liveBefore && fs::is_empty(stage) &&
          std::distance(fs::directory_iterator(root), fs::directory_iterator{}) == 2,
          "The baseline filename failure leaves the live collection, empty stage and recovery namespace untouched");
  Require(ReadSmallFile(typographic) == archiveBytes, "The baseline filename failure preserves the exact source ZIP bytes");
  std::cout << "LEGACY_ZIP_CASE=smart-apostrophe result=expected-refusal\n";
}

static std::array<DWORD, 3> FixtureIdentity(const fs::path &path) {
  NoReparseAncestors(path);
  const ScopedKernelHandle handle(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS, nullptr));
  BY_HANDLE_FILE_INFORMATION info{};
  if (!handle || !GetFileInformationByHandle(handle.get(), &info))
    throw std::runtime_error("Could not identify the exact private ZIP fixture object");
  return {info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow};
}

static void RangeLockRefusalAndRetry(bool before, const Tree &expected) {
  const auto root = Seed(L"range-lock");
  const auto archive = fixture / L"flat.zip";
  const auto stage = root / L"stage";
  const auto live = root / L"Sites";
  const auto oldNote = live / L"Previous Client/original.txt";
  const auto liveBefore = Snapshot(live);
  const auto archiveBytes = ReadSmallFile(archive);
  const auto archiveIdentity = FixtureIdentity(archive);
  const auto stageIdentity = FixtureIdentity(stage);
  const auto liveIdentity = FixtureIdentity(live);
  const auto noteIdentity = FixtureIdentity(oldNote);
  constexpr DWORD zipEndBytes = 22;
  Require(archiveBytes.size() > zipEndBytes &&
          archiveBytes.compare(archiveBytes.size() - zipEndBytes, 4, "PK\x05\x06") == 0,
          "The ordinary ZIP has the expected final central directory record");
  {
    const ScopedKernelHandle blocker(CreateFileW(archive.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    OVERLAPPED range{};
    range.Offset = static_cast<DWORD>(archiveBytes.size() - zipEndBytes);
    Require(blocker && LockFileEx(blocker.get(), LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                                 0, zipEndBytes, 0, &range),
            "Could not acquire the genuine exclusive byte-range lock on the valid private ZIP tail");
    {
      const ScopedKernelHandle probe(CreateFileW(archive.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
      Require(static_cast<bool>(probe), "A compatible second archive open must succeed under the range lock");
      char byte = 0;
      DWORD count = 0;
      Require(ReadFile(probe.get(), &byte, 1, &count, nullptr) && count == 1 && byte == archiveBytes.front(),
              "The independently opened archive can still read its first byte");
      LARGE_INTEGER tail{};
      tail.QuadPart = range.Offset;
      Require(SetFilePointerEx(probe.get(), tail, nullptr, FILE_BEGIN),
              "Could not seek the independent ZIP reader to the locked tail");
      const bool read = ReadFile(probe.get(), &byte, 1, &count, nullptr) != FALSE;
      const auto error = read ? ERROR_SUCCESS : GetLastError();
      Require(!read && error == ERROR_LOCK_VIOLATION, "Independent ZIP tail read must see the native lock violation");
      std::cout << "LEGACY_ZIP_RANGE_NATIVE_ERROR=" << error << '\n';
    }

    const auto result = Extract(archive, stage, 1);
    std::cout << "LEGACY_ZIP_RANGE_HRESULT=0x" << std::hex
              << static_cast<unsigned long>(result.code) << std::dec << '\n';
    if (before) {
      Require(result.code == E_FAIL &&
              result.details.find(L"Legacy ZIP validation or extraction failed (exit code ") != std::wstring::npos,
              "The baseline collapses the genuine ZIP tail lock into a generic child error");
      std::cout << "LEGACY_ZIP_RANGE_GENERIC_BEFORE=1\n";
    } else {
      Require(result.code == HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION) &&
              result.details.find(L"in use") != std::wstring::npos,
              "The repaired ZIP path preserves the byte-range lock HRESULT and in-use message");
      std::cout << "LEGACY_ZIP_RANGE_IN_USE_AFTER=1\n";
    }
    Require(Snapshot(live) == liveBefore && FixtureIdentity(live) == liveIdentity &&
            FixtureIdentity(oldNote) == noteIdentity && fs::is_empty(stage) &&
            FixtureIdentity(stage) == stageIdentity &&
            std::distance(fs::directory_iterator(root), fs::directory_iterator{}) == 2,
            "ZIP tail-lock refusal preserved live data and the exact empty stage without recovery moves");
    Require(UnlockFileEx(blocker.get(), 0, zipEndBytes, 0, &range),
            "Could not release the private ZIP tail lock for the retry");
  }

  Require(ReadSmallFile(archive) == archiveBytes && FixtureIdentity(archive) == archiveIdentity,
          "ZIP tail-lock refusal preserved the exact source archive");
  std::cout << "LEGACY_ZIP_RANGE_PRESERVED=1\n";
  SuccessfulRestore("range-retry", root, archive, false, 1, expected);
  Require(FixtureIdentity(archive) == archiveIdentity, "Unlocked retry used the same original ZIP object");
  std::cout << "LEGACY_ZIP_CASE=range-lock result=passed\n";
}

static void LiteralStagingPath(bool before, const Tree &expected) {
  const auto root = Seed(L"literal [archive]");
  const auto archive = fixture / L"flat.zip";
  const auto stage = root / L"stage";
  const auto live = root / L"Sites";
  const auto note = live / L"Previous Client/original.txt";
  const auto liveBefore = Snapshot(live);
  const auto archiveBytes = ReadSmallFile(archive);
  const auto archiveIdentity = FixtureIdentity(archive);
  const auto stageIdentity = FixtureIdentity(stage);
  const auto liveIdentity = FixtureIdentity(live);
  const auto noteIdentity = FixtureIdentity(note);
  Require(root.filename() == L"literal [archive]" && fs::is_empty(stage),
          "The ordinary ZIP uses an existing stage under a literal bracket directory");
  if (before) {
    const auto result = Extract(archive, stage, 1);
    std::cout << "LEGACY_ZIP_LITERAL_DESTINATION_HRESULT=0x" << std::hex
              << static_cast<unsigned long>(result.code) << std::dec << '\n';
    Require(result.code == E_FAIL &&
            result.details.find(L"Legacy ZIP validation or extraction failed (exit code ") != std::wstring::npos,
            "The baseline hidden child rejects an ordinary pre-created bracket staging path");
    Require(Snapshot(live) == liveBefore && FixtureIdentity(live) == liveIdentity &&
            FixtureIdentity(note) == noteIdentity && fs::is_empty(stage) &&
            FixtureIdentity(stage) == stageIdentity &&
            std::distance(fs::directory_iterator(root), fs::directory_iterator{}) == 2,
            "Destination refusal preserves the exact live collection and empty stage without recovery moves");
    std::cout << "LEGACY_ZIP_CASE=literal-stage-path result=expected-refusal\n";
  } else {
    SuccessfulRestore("literal-stage-path", root, archive, false, 1, expected);
    bool originalRecovered = false;
    for (const auto &entry : fs::directory_iterator(root)) {
      const auto recoveredNote = entry.path() / L"Previous Client/original.txt";
      if (!fs::is_regular_file(recoveredNote)) continue;
      originalRecovered = FixtureIdentity(entry.path()) == liveIdentity &&
                          FixtureIdentity(recoveredNote) == noteIdentity;
    }
    Require(originalRecovered, "Bracket-path restore moves the exact original directory and note into recovery");
  }
  Require(ReadSmallFile(archive) == archiveBytes && FixtureIdentity(archive) == archiveIdentity,
          "Bracket-path processing preserves the exact ordinary ZIP");
  std::cout << "LEGACY_ZIP_LITERAL_DESTINATION_PRESERVED=1\n";
}

enum class FlatSitesLayout { CurrentSchema, BoundLegacyBrowser, UnmarkedLegacyBrowser };

static void FlatSitesClient(bool before, FlatSitesLayout layout) {
  const bool boundLegacyBrowser = layout == FlatSitesLayout::BoundLegacyBrowser;
  const bool unmarkedLegacyBrowser = layout == FlatSitesLayout::UnmarkedLegacyBrowser;
  const bool legacyBrowser = boundLegacyBrowser || unmarkedLegacyBrowser;
  const std::wstring clientName = legacyBrowser ? L"Sites" : L"sItEs";
  const wchar_t *caseName = boundLegacyBrowser ? L"flat-bound-sites" :
      unmarkedLegacyBrowser ? L"flat-unmarked-sites" : L"flat-sites";
  const auto root = Seed(caseName);
  const auto archive = fixture / (std::wstring(caseName) + L".zip");
  const auto stage = root / L"stage";
  const auto live = root / L"Sites";
  const auto oldNote = live / L"Previous Client/original.txt";
  const auto liveBefore = Snapshot(live);
  const auto liveIdentity = FixtureIdentity(live);
  const auto noteIdentity = FixtureIdentity(oldNote);
  const auto stageIdentity = FixtureIdentity(stage);
  const auto archiveBytes = ReadSmallFile(archive);
  const auto archiveIdentity = FixtureIdentity(archive);
  Tree expected{{L"Beta", std::nullopt}, {L"Beta/content.txt", "FLAT BETA\n"},
                {clientName, std::nullopt}};
  if (legacyBrowser) {
    expected.emplace(L"Sites/Default", std::nullopt);
    expected.emplace(L"Sites/Default/Preferences", "{\"profile\":{\"name\":\"Legacy Sites\"}}\n");
    expected.emplace(L"Sites/Local State", "{\"profile\":{\"last_used\":\"Default\"}}\n");
    if (boundLegacyBrowser)
      expected.emplace(L"Sites/ctSpaces-legacy-browser-v2", "ctSpaces-legacy-browser-schema=2\r\nbrowser=edge\r\n");
  } else {
    expected.emplace(L"sItEs/content.txt", "FLAT SITES CLIENT\n");
    expected.emplace(L"sItEs/ctSpaces-client-v2", "ctSpaces-client-schema=2\r\n");
  }
  Require(SanitizeName(clientName) == clientName &&
          CtBackup::Detail::IsValidClientDirectoryName(clientName),
          "The launcher and backup engine both accept this ordinary client name");
  Require(Extract(archive, stage, 1).ok(), "The ordinary flat ZIP extracts in the real hidden child");
  std::optional<BrowserKind> binding;
  const auto extractedClient = stage / clientName;
  const bool supportedClientLayout = legacyBrowser
      ? IsSafeHybridClientRoot(extractedClient, &binding) &&
        (boundLegacyBrowser ? binding == BrowserKind::Edge : !binding)
      : IsV2ClientContainer(extractedClient);
  Require(Snapshot(stage) == expected && supportedClientLayout,
          "The flat collection contains two real clients and a recognized Sites-client layout");
  CtBackup::StagedRestore staged;
  const auto result = NormalizeLegacyZipStaging(stage, staged);
  std::cout << (boundLegacyBrowser ? "LEGACY_ZIP_FLAT_BOUND_SITES_HRESULT=0x" :
                unmarkedLegacyBrowser ? "LEGACY_ZIP_FLAT_UNMARKED_SITES_HRESULT=0x" :
                                    "LEGACY_ZIP_FLAT_SITES_HRESULT=0x") << std::hex
            << static_cast<unsigned long>(result.code) << std::dec << '\n';
  if (before) {
    Require(result.code == E_INVALIDARG &&
            result.details == L"The legacy backup contains unexpected root data." &&
            !staged.validation && staged.stagingRoot.empty(),
            "The baseline mistakes the Sites client for a wrapper and refuses the valid flat collection");
    Require(Snapshot(stage) == expected && FixtureIdentity(stage) == stageIdentity &&
            Snapshot(live) == liveBefore && FixtureIdentity(live) == liveIdentity &&
            FixtureIdentity(oldNote) == noteIdentity &&
            std::distance(fs::directory_iterator(root), fs::directory_iterator{}) == 2,
            "The refusal leaves the extracted and live trees untouched without recovery moves");
  } else {
    Require(result.ok() && staged.validation && staged.profileCount == 2 &&
            staged.payloadDir == stage / L"Sites", "The flat collection seals under its canonical wrapper");
    Require(Snapshot(staged.payloadDir) == expected && FixtureIdentity(stage) == stageIdentity &&
            Snapshot(live) == liveBefore && FixtureIdentity(live) == liveIdentity,
            "Normalization preserves both clients and their spelling while leaving the live tree untouched");
    DWORD recoveryError = ERROR_SUCCESS;
    const auto recovery = UniqueRecoveryPath(root, L"2026-11-12_120000", &recoveryError);
    Require(recoveryError == ERROR_SUCCESS && recovery.parent_path() == root && !fs::exists(recovery),
            "The restore chooses an unused private recovery sibling");
    unsigned commitGuards = 0;
    Require(CtBackup::CommitStagedRestore(staged, live, recovery, CountFinalGuard, &commitGuards).ok(),
            "The production transaction installs the sealed flat collection");
    Require(commitGuards == 1 && Snapshot(live) == expected && !fs::exists(stage) &&
            Snapshot(recovery) == liveBefore && FixtureIdentity(recovery) == liveIdentity &&
            FixtureIdentity(recovery / L"Previous Client/original.txt") == noteIdentity,
            "Commit installs both exact clients and retains the exact previous collection in recovery");
  }
  Require(ReadSmallFile(archive) == archiveBytes && FixtureIdentity(archive) == archiveIdentity,
          "The flat Sites-client restore preserves the exact source ZIP");
  std::cout << (boundLegacyBrowser ? "LEGACY_ZIP_CASE=flat-bound-sites-client result=" :
                unmarkedLegacyBrowser ? "LEGACY_ZIP_CASE=flat-unmarked-sites-client result=" :
                                    "LEGACY_ZIP_CASE=flat-sites-client result=")
            << (before ? "expected-refusal" : "passed") << '\n';
}

static void DrainAudit() {
  MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    if ((message.message >= WM_KEYFIRST && message.message <= WM_KEYLAST) ||
        (message.message >= WM_MOUSEFIRST && message.message <= WM_MOUSELAST)) ++forbidden;
    else DispatchMessageW(&message);
  }
}
} // namespace legacy_zip_test

int wmain(int argc, wchar_t **argv) {
  using namespace legacy_zip_test;
  HHOOK veto = nullptr;
  const auto releaseHooks = [&] {
    for (const auto hook : observers) UnhookWinEvent(hook);
    if (veto) UnhookWindowsHookEx(veto);
  };
  try {
    std::cout << std::unitbuf;
    bool validArguments = argc >= 4 && argc <= 14 && std::wstring_view(argv[1]) == L"--headless" &&
        (std::wstring_view(argv[3]) == L"--sharing-before" || std::wstring_view(argv[3]) == L"--sharing-after");
    std::optional<bool> filenameBefore, rangeBefore, destinationBefore, flatSitesBefore, boundSitesBefore;
    bool markedSitesOnly = false;
    bool windowsEntryPaths = false;
    bool zipWriteTimes = false;
    bool unicodeEntryNames = false;
    std::optional<bool> unmarkedSitesBefore;
    for (int i = 4; validArguments && i < argc; ++i) {
      const std::wstring_view proof = argv[i];
      if (!filenameBefore.has_value() && (proof == L"--filename-before" || proof == L"--filename-after"))
        filenameBefore = proof == L"--filename-before";
      else if (!rangeBefore.has_value() && (proof == L"--range-before" || proof == L"--range-after"))
        rangeBefore = proof == L"--range-before";
      else if (!destinationBefore.has_value() &&
               (proof == L"--destination-before" || proof == L"--destination-after"))
        destinationBefore = proof == L"--destination-before";
      else if (!flatSitesBefore.has_value() &&
               (proof == L"--flat-sites-before" || proof == L"--flat-sites-after"))
        flatSitesBefore = proof == L"--flat-sites-before";
      else if (!markedSitesOnly && proof == L"--marked-sites-only")
        markedSitesOnly = true;
      else if (!boundSitesBefore &&
               (proof == L"--bound-sites-before" || proof == L"--bound-sites-after"))
        boundSitesBefore = proof == L"--bound-sites-before";
      else if (!windowsEntryPaths && proof == L"--windows-entry-paths")
        windowsEntryPaths = true;
      else if (!zipWriteTimes && proof == L"--zip-write-times")
        zipWriteTimes = true;
      else if (!unicodeEntryNames && proof == L"--unicode-entry-names")
        unicodeEntryNames = true;
      else if (!unmarkedSitesBefore &&
               (proof == L"--unmarked-sites-before" || proof == L"--unmarked-sites-after"))
        unmarkedSitesBefore = proof == L"--unmarked-sites-before";
      else
        validArguments = false;
    }
    Require(validArguments, "Explicit headless fixture and supported proof modes required");
    const bool before = std::wstring_view(argv[3]) == L"--sharing-before";
    const bool checkFilenames = filenameBefore.has_value();
    const bool checkRange = rangeBefore.has_value();
    const bool checkDestination = destinationBefore.has_value();
    fixture = fs::path(argv[2]);
    const fs::path originalFixture(CTSPACES_LEGACY_ZIP_FIXTURE_ROOT);
    const auto leaf = fixture.filename().native();
    const bool roundLeaf = leaf.size() == 1 && ((leaf.front() >= L'0' && leaf.front() <= L'9') ||
                                               (leaf.front() >= L'a' && leaf.front() <= L'z'));
    Require(fixture.is_absolute() && fixture.lexically_normal() == fixture &&
            (fixture == originalFixture || (roundLeaf && fixture.parent_path() == originalFixture.parent_path())),
            "Only the compile-bound workspace fixture and its one-character round siblings are allowed");
    NoReparseAncestors(fixture);
    Require(ReadSmallFile(fixture / L"scope.txt") == "CTSPACES ORDINARY LEGACY ZIP FIXTURE\n", "Private fixture marker is missing");
    std::array<wchar_t, 32768> system{};
    const UINT length = GetSystemDirectoryW(system.data(), static_cast<UINT>(system.size()));
    Require(length && length < system.size(), "System directory is unavailable");
    systemPowerShell = fs::path(system.data()) / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";
    Require(fs::is_regular_file(systemPowerShell), "Fixed Windows PowerShell executable is unavailable");
    veto = SetWindowsHookExW(WH_CBT, VetoActivation, nullptr, GetCurrentThreadId());
    Require(veto && ObserveProcess(GetCurrentProcessId()), "Could not install test-only native interaction audit");
    const Tree wrapped{{L"Alpha", std::nullopt}, {L"Alpha/content.txt", "WRAPPED ALPHA\n"},
      {L"Alpha/empty", std::nullopt}, {L"\u00C9quipe QA", std::nullopt},
      {L"\u00C9quipe QA/profile", std::nullopt}, {L"\u00C9quipe QA/profile/state.bin", "WRAPPED UNICODE CLIENT\n"}};
    SuccessfulRestore("wrapped", Seed(L"wrapped"), fixture / L"wrapped archive.zip", true, 2, wrapped);
    const Tree flat{{L"Legacy Client", std::nullopt}, {L"Legacy Client/content.txt", "FLAT CLIENT\n"},
                    {L"Legacy Client/empty", std::nullopt}};
    SuccessfulRestore("flat", Seed(L"flat"), fixture / L"flat.zip", false, 1, flat);
    MixedLayoutRefusal();
    MissingArchiveRefusal();
    SharingRefusalAndRetry(before, flat);
    if (checkFilenames)
      ApostropheFilenames(*filenameBefore, flat);
    if (checkRange)
      RangeLockRefusalAndRetry(*rangeBefore, flat);
    if (checkDestination)
      LiteralStagingPath(*destinationBefore, flat);
    if (flatSitesBefore)
      FlatSitesClient(*flatSitesBefore, FlatSitesLayout::CurrentSchema);
    if (boundSitesBefore)
      FlatSitesClient(*boundSitesBefore, FlatSitesLayout::BoundLegacyBrowser);
    if (unmarkedSitesBefore)
      FlatSitesClient(*unmarkedSitesBefore, FlatSitesLayout::UnmarkedLegacyBrowser);
    if (markedSitesOnly) {
      const Tree single{{L"sItEs", std::nullopt}, {L"sItEs/Browsers", std::nullopt},
          {L"sItEs/Browsers/edge", std::nullopt}, {L"sItEs/Browsers/edge/Profile", std::nullopt},
          {L"sItEs/Browsers/edge/Profile/Default", std::nullopt},
          {L"sItEs/Browsers/edge/Profile/Default/Preferences", "{\"profile\":{\"name\":\"Legacy Sites\"}}\n"},
          {L"sItEs/Browsers/edge/Profile/ctSpaces", "ctSpaces-profile=2\r\n"},
          {L"sItEs/Browsers/edge/ctSpaces-browser-v2", "ctSpaces-browser-schema=2\r\nbrowser=edge\r\n"},
          {L"sItEs/ctSpaces-client-v2", "ctSpaces-client-schema=2\r\n"}};
      SuccessfulRestore("marked-sites-only", Seed(L"marked-sites-only"),
                        fixture / L"marked-sites-only.zip", true, 1, single);
    }
    if (windowsEntryPaths) {
      const Tree windowsTree{{L"Windows Client", std::nullopt},
          {L"Windows Client/Browser Profile", std::nullopt},
          {L"Windows Client/Browser Profile/Preferences", "{\"profile\":{\"name\":\"Windows Client\"}}\n"},
          {L"Windows Client/Browser Profile/State.v2", std::nullopt}};
      SuccessfulRestore("windows-entry-paths", Seed(L"windows-entry-paths"),
                        fixture / L"Windows archive.ZIP", true, 1, windowsTree);
    }
    if (zipWriteTimes) {
      const Tree timestampTree{{L"MiXeD Client", std::nullopt},
          {L"MiXeD Client/Preferences", "OLDER WINDOWS ZIP PREFERENCES\n"},
          {L"MiXeD Client/Empty Log", ""}};
      const std::map<fs::path, SYSTEMTIME> writeTimes{
          {L"MiXeD Client/Preferences", {2002, 1, 0, 18, 12, 4, 56, 0}},
          {L"MiXeD Client/Empty Log", {2024, 11, 0, 19, 16, 32, 10, 0}}};
      SuccessfulRestore("zip-write-times", Seed(L"zip-write-times"),
                        fixture / L"archive-times.zip", true, 1, timestampTree, writeTimes);
    }
    if (unicodeEntryNames) {
      const std::wstring client = L"\u6771\u4EAC \u00C9quipe \U0001F4BC";
      const std::wstring file = L"\u0394ossier \U0001F4C4.txt";
      const Tree names{{client, std::nullopt}, {client + L"/" + file, "MIXED SCRIPT CLIENT\n"},
                      {client + L"/\u0410\u0440\u0445\u0438\u0432", std::nullopt}};
      SuccessfulRestore("unicode-entry-names", Seed(L"unicode-entry-names"),
          fixture / L"\u6771\u4EAC \U0001F4E6 backup.zip", true, 1, names);
    }
    DrainAudit();
    const unsigned expectedChildren = (before ? 5 : 4) + (checkFilenames ? 2 : 0) +
        (checkRange ? 2 : 0) + (checkDestination ? 1 : 0) + (flatSitesBefore ? 1 : 0) +
        (markedSitesOnly ? 1 : 0) + (boundSitesBefore ? 1 : 0) +
        (windowsEntryPaths ? 1 : 0) + (zipWriteTimes ? 1 : 0) + (unmarkedSitesBefore ? 1 : 0) +
        (unicodeEntryNames ? 1 : 0);
    Require(childAttempts == expectedChildren && childLaunches == expectedChildren && childPids.size() == expectedChildren &&
            !forbidden && !shows && !focuses && !foreground,
            "Legacy ZIP coverage must use only its expected owned children and have zero native interaction events");
    releaseHooks();
    std::cout << "VERSION=" << CTSPACES_VERSION_TEXT << "\nCHECKS=" << checks
              << "\nCASES=" << (checkFilenames ? 7 : 5) + (checkRange ? 1 : 0) +
                                 (checkDestination ? 1 : 0) + (flatSitesBefore ? 1 : 0) +
                                 (markedSitesOnly ? 1 : 0) + (boundSitesBefore ? 1 : 0) +
                                 (windowsEntryPaths ? 1 : 0) + (zipWriteTimes ? 1 : 0) +
                                 (unmarkedSitesBefore ? 1 : 0) + (unicodeEntryNames ? 1 : 0)
              << "\nFAILURES=0\n"
              << "EXPECTED_SYSTEM_POWERSHELL_CHILDREN=" << expectedChildren
              << "\nNATIVE_EVENTS=0/0/0\nFORBIDDEN=0\n";
    return 0;
  } catch (const std::exception &error) {
    releaseHooks();
    std::cerr << "Legacy ZIP test failed: " << error.what() << "\nCHECKS=" << checks
              << "\nCHILD_ATTEMPTS=" << childAttempts << "\nCHILD_LAUNCHES=" << childLaunches
              << "\nNATIVE_EVENTS=" << shows << '/' << focuses << '/' << foreground
              << "\nFORBIDDEN=" << forbidden << '\n';
    return 1;
  }
}
