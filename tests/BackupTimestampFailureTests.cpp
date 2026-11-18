// Ordinary valid-backup root-access and timestamp-error regression. No UI,
// ACL changes, malformed inputs, external profiles, or fixture cleanup.
#include "../BackupEngine.h"
#include "../RestoreIoPath.h"
#include "../version.h"
#include "BackupTimestampFailureNativeBoundary.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>

#ifndef CTSPACES_TIMESTAMP_FAILURE_FIXTURE_PARENT
#error Bind this test to a new workspace artifact directory through its wrapper.
#endif
#ifdef CTSPACES_ARCHIVE_ITEM_LIMIT_FOR_TEST
#error This regression must use the ordinary production archive limit.
#endif
static_assert(CtArchiveSafety::kMaxItems == 500'000);
namespace fs = std::filesystem;
namespace {
unsigned checks = 0;
void Check(bool value, const char* message) {
  ++checks;
  std::cout << (value ? "PASS " : "FAIL ") << message << std::endl;
  if (!value) throw std::runtime_error(message);
}
void NoReparseAncestors(const fs::path& path) {
  for (auto current = path; !current.empty();) {
    const DWORD attributes = GetFileAttributesW(current.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
      throw std::runtime_error("Reparse fixture ancestor refused.");
    const auto parent = current.parent_path();
    if (parent == current) break;
    current = parent;
  }
}
fs::path ReserveFixture(const wchar_t* requested) {
  const auto parent = fs::path(CTSPACES_TIMESTAMP_FAILURE_FIXTURE_PARENT).lexically_normal();
  const fs::path raw(requested);
  if (!parent.is_absolute() || !raw.is_absolute()) throw std::runtime_error("Absolute fixture paths required.");
  for (const auto& part : raw)
    if (part == L"." || part == L"..") throw std::runtime_error("Fixture dot components refused.");
  const auto fixture = raw.lexically_normal();
  if (_wcsicmp(fixture.parent_path().c_str(), parent.c_str()) != 0 ||
      fixture.filename().empty() || fixture.filename().native().size() > 12)
    throw std::runtime_error("Use a fresh short direct child of the compile-bound artifact directory.");
  NoReparseAncestors(fixture);
  if (GetFileAttributesW(fixture.c_str()) != INVALID_FILE_ATTRIBUTES || !CreateDirectoryW(fixture.c_str(), nullptr))
    throw std::runtime_error("Existing fixture refused; all evidence is retained.");
  NoReparseAncestors(fixture);
  return fixture;
}
std::string Read(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Private fixture read failed.");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void WriteNew(const fs::path& path, const std::string& bytes) {
  fs::create_directories(path.parent_path());
  HANDLE output = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (output == INVALID_HANDLE_VALUE) throw std::runtime_error("Private fixture CREATE_NEW failed.");
  DWORD written = 0;
  const auto size = static_cast<DWORD>(bytes.size());
  const bool ok = WriteFile(output, bytes.data(), size, &written, nullptr) && written == size && FlushFileBuffers(output);
  CloseHandle(output);
  if (!ok) throw std::runtime_error("Private fixture write failed.");
}
void Status(const wchar_t* name, const CtBackup::Result& result) {
  std::wcout << name << L"=0x" << std::hex << std::setw(8) << std::setfill(L'0')
             << static_cast<unsigned long>(result.code) << std::dec << L"\nDETAILS=" << result.details << L"\n";
}
std::wstring Normalize(fs::path path) {
  auto text = path.lexically_normal().native();
  if (text.starts_with(L"\\\\?\\")) text.erase(0, 4);
  while (text.size() > 3 && (text.back() == L'\\' || text.back() == L'/')) text.pop_back();
  for (auto& value : text) if (value == L'/') value = L'\\';
  return text;
}
bool SamePath(const fs::path& left, const fs::path& right) {
  return _wcsicmp(Normalize(left).c_str(), Normalize(right).c_str()) == 0;
}
enum class Fault { None, RootAttributes, FileTimestamp, DirectoryTimestamp };
struct Boundary {
  Fault fault = Fault::None;
  fs::path target;
  unsigned denied = 0, rootOpens = 0, matchingTimestampCalls = 0;
} boundary;
void Arm(Fault fault, const fs::path& target) { boundary = {fault, target}; }
struct Entry {
  bool directory = false;
  DWORD volume = 0, high = 0, low = 0, attributes = 0;
  unsigned long long creation = 0, write = 0;
  std::string bytes;
  bool operator==(const Entry&) const = default;
};
unsigned long long Number(FILETIME time) {
  return (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}
Entry Capture(const fs::path& path) {
  NoReparseAncestors(path);
  HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Private inventory binding failed.");
  BY_HANDLE_FILE_INFORMATION info{};
  const bool ok = GetFileInformationByHandle(file, &info) && GetFileType(file) == FILE_TYPE_DISK &&
      !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
      ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || info.nNumberOfLinks == 1);
  CloseHandle(file);
  if (!ok) throw std::runtime_error("Unsafe private inventory object refused.");
  Entry result;
  result.directory = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  result.attributes = info.dwFileAttributes;
  result.volume = info.dwVolumeSerialNumber; result.high = info.nFileIndexHigh; result.low = info.nFileIndexLow;
  result.creation = Number(info.ftCreationTime); result.write = Number(info.ftLastWriteTime);
  if (!result.directory) result.bytes = Read(path);
  return result;
}
struct Tree {
  bool exists = false;
  std::map<std::wstring, Entry> entries;
  bool operator==(const Tree&) const = default;
};
Tree Snapshot(const fs::path& root) {
  NoReparseAncestors(root);
  const DWORD attributes = GetFileAttributesW(root.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    const DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return {};
    throw std::runtime_error("Private snapshot root unreadable.");
  }
  Tree tree; tree.exists = true; tree.entries.emplace(L"", Capture(root));
  if (attributes & FILE_ATTRIBUTE_DIRECTORY)
    for (const auto& entry : fs::recursive_directory_iterator(root))
      tree.entries.emplace(entry.path().lexically_relative(root).native(), Capture(entry.path()));
  return tree;
}
bool SamePayload(const Tree& left, const Tree& right) {
  if (!left.exists || !right.exists || left.entries.size() != right.entries.size()) return false;
  for (const auto& [name, item] : left.entries) {
    const auto found = right.entries.find(name);
    if (found == right.entries.end()) return false;
    const auto& other = found->second;
    if (item.directory != other.directory || item.bytes != other.bytes ||
        item.creation != other.creation || item.write != other.write) return false;
  }
  return true;
}
void HistoricalTimes(const fs::path& path, unsigned ordinal) {
  SYSTEMTIME creation{}, write{};
  creation.wYear = 2004; creation.wMonth = 3; creation.wDay = 2; creation.wHour = 6;
  write.wYear = 2012; write.wMonth = 7; write.wDay = 8; write.wHour = 9;
  creation.wSecond = static_cast<WORD>(ordinal); write.wSecond = static_cast<WORD>(ordinal);
  FILETIME created{}, modified{};
  if (!SystemTimeToFileTime(&creation, &created) || !SystemTimeToFileTime(&write, &modified))
    throw std::runtime_error("Historical times unavailable.");
  HANDLE file = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Timestamp fixture binding failed.");
  const bool ok = SetFileTime(file, &created, nullptr, &modified) != FALSE;
  CloseHandle(file);
  if (!ok) throw std::runtime_error("Fixture timestamps could not be assigned.");
}
struct Fixture {
  fs::path root, source, archive, live, recovery;
  Tree sourceBefore, archiveBefore, liveBefore, recoveryBefore;
  explicit Fixture(const fs::path& base) : root(base), source(base / L"s/Sites"), archive(base / L"o/b.7z"),
      live(base / L"l/Sites"), recovery(base / L"l/R") {
    fs::create_directories(archive.parent_path());
    fs::create_directories(source / L"Alpha/empty");
    WriteNew(source / L"Alpha/nested/file.txt", "ordinary timestamp failure payload\r\n");
    WriteNew(source / L"Alpha/other.txt", "independent ordinary client content\r\n");
    WriteNew(live / L"Previous/old.txt", "retained original live content\r\n");
    const std::array<fs::path, 6> names{L"Alpha/nested/file.txt", L"Alpha/other.txt", L"Alpha/empty", L"Alpha/nested", L"Alpha", L""};
    for (unsigned i = 0; i < names.size(); ++i) HistoricalTimes(source / names[i], i);
    HistoricalTimes(live / L"Previous/old.txt", 12);
    sourceBefore = Snapshot(source); liveBefore = Snapshot(live); recoveryBefore = Snapshot(recovery);
    Check(CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(live, L"Alpha"), "Fixture final target fits production path limits");
    const auto result = CtBackup::CreateValidated7zBackup(source, archive, CTSPACES_VERSION_TEXT, nullptr, nullptr);
    Status(L"ORDINARY_BACKUP_HRESULT", result);
    Check(result.ok(), "Actual backend and SDK produce an ordinary valid backup");
    _7zArchiveInfo info{};
    Check(_7zInspect7z(archive.c_str(), &info) == S_OK && info.sitesProfileCount == 1 &&
          _7zTest7z(archive.c_str(), nullptr, nullptr) == S_OK, "Ordinary backup passes actual inspection and full CRC");
    archiveBefore = Snapshot(archive);
    Check(Snapshot(source) == sourceBefore, "Backup preserves original source bytes, metadata and identities");
  }
  void Unchanged() const {
    Check(Snapshot(source) == sourceBefore && Snapshot(archive) == archiveBefore &&
          Snapshot(live) == liveBefore && Snapshot(recovery) == recoveryBefore,
          "Failure leaves source, archive, live collection and recovery bytes/metadata/identities unchanged");
  }
  void RetryAndCommit() const {
    CtBackup::StagedRestore staged;
    const auto result = CtBackup::StageValidated7zRestore(archive, root / L"retry", nullptr, nullptr, staged);
    Status(L"FRESH_RETRY_STAGE_HRESULT", result);
    Check(result.ok() && SamePayload(sourceBefore, Snapshot(staged.payloadDir)), "Fresh retry stages every exact payload byte and original timestamp");
    Check(_7zTest7z(archive.c_str(), nullptr, nullptr) == S_OK && Snapshot(archive) == archiveBefore,
          "Paired original archive stays byte-exact and CRC-valid before retry commit");
    const auto committed = CtBackup::CommitStagedRestore(staged, live, recovery);
    Status(L"FRESH_RETRY_COMMIT_HRESULT", committed);
    Check(committed.ok() && SamePayload(sourceBefore, Snapshot(live)), "Fresh retry commits exact payload bytes and original timestamps");
    Check(Snapshot(recovery) == liveBefore && Snapshot(source) == sourceBefore && Snapshot(archive) == archiveBefore,
          "Retry retains original live bytes, metadata and identities in recovery without changing source/archive");
  }
};
void RootCase(const fs::path& root, bool before) {
  Fixture fixture(root);
  const auto output = root / L"wrapper";
  Check(CreateDirectoryW(output.c_str(), nullptr) != FALSE && fs::is_empty(output), "Generic extraction starts with a preexisting empty wrapper root");
  _7zArchiveInfo info{}; _7zRestoreSession* raw = nullptr;
  Check(_7zOpenRestoreSession(fixture.archive.c_str(), &info, &raw) == S_OK && raw,
        "Generic extraction opens the real read-only archive session");
  struct Session { _7zRestoreSession* value; ~Session() { _7zCloseRestoreSession(value); } } session{raw};
  Arm(Fault::RootAttributes, output);
  std::wstring detail;
  const HRESULT result = _7zExtractRestoreSession(raw, output.c_str(), nullptr, nullptr, false, INVALID_HANDLE_VALUE, &detail);
  const auto observation = boundary; boundary = {};
  Status(L"GENERIC_ROOT_EXTRACT_HRESULT", {result, detail});
  Check(observation.rootOpens > 0, "Native boundary observed the actual preexisting wrapper-root open");
  if (before) {
    Check(result == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED) && observation.denied == 1 && fs::is_empty(output),
          "Before root fix the needless wrapper-root attribute request reproduces access denied");
    std::cout << "EXPECTED_ROOT_DEFECT_REPRODUCED=1\n";
  } else {
    Check(result == S_OK && observation.denied == 0 && SamePayload(fixture.sourceBefore, Snapshot(output / L"Sites")),
          "Generic extraction succeeds without requesting wrapper-root attribute writes and preserves payload timestamps");
  }
  fixture.Unchanged();
  std::cout << "ROOT_CASE_COMPLETED=1\n";
}
void TimestampCase(const fs::path& root, bool directory) {
  Fixture fixture(root);
  const auto stage = root / L"failed";
  Arm(directory ? Fault::DirectoryTimestamp : Fault::FileTimestamp,
      stage / (directory ? L"Sites/Alpha/nested" : L"Sites/Alpha/nested/file.txt"));
  CtBackup::StagedRestore staged;
  const auto result = CtBackup::StageValidated7zRestore(fixture.archive, stage, nullptr, nullptr, staged);
  const auto observation = boundary; boundary = {};
  Status(directory ? L"DIRECTORY_TIME_FAILURE_HRESULT" : L"FILE_TIME_FAILURE_HRESULT", result);
  Check(observation.denied == 1 && observation.matchingTimestampCalls == 1 &&
        result.code == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED) && result.details.find(L"timestamps") != std::wstring::npos &&
        result.details.find(directory ? L"directory" : L"file") != std::wstring::npos &&
        result.details.find(L"nested") != std::wstring::npos && !staged.validation,
        directory ? "Actual directory finalization denial preserves its HRESULT/item diagnostic and cannot seal partial staging" :
                    "Actual output-file timestamp denial preserves its HRESULT/item diagnostic and cannot seal partial staging");
  fixture.Unchanged();
  fixture.RetryAndCommit();
  std::cout << (directory ? "DIRECTORY_FAILURE_CASE_COMPLETED=1\n" : "FILE_FAILURE_CASE_COMPLETED=1\n");
}
}

namespace backup_timestamp_boundary {
HANDLE WINAPI CreateFileW(LPCWSTR path, DWORD access, DWORD sharing, LPSECURITY_ATTRIBUTES security,
                          DWORD disposition, DWORD flags, HANDLE templateFile) {
  if (boundary.fault == Fault::RootAttributes && path && disposition == OPEN_EXISTING && SamePath(path, boundary.target)) {
    ++boundary.rootOpens;
    if (access & FILE_WRITE_ATTRIBUTES) { ++boundary.denied; ::SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
  }
  return ::CreateFileW(path, access, sharing, security, disposition, flags, templateFile);
}
BOOL WINAPI SetFileTime(HANDLE file, const FILETIME* creation, const FILETIME* access, const FILETIME* write) {
  if (boundary.fault == Fault::FileTimestamp || boundary.fault == Fault::DirectoryTimestamp) {
    wchar_t name[32768]{};
    const DWORD count = ::GetFinalPathNameByHandleW(file, name, static_cast<DWORD>(std::size(name)), FILE_NAME_NORMALIZED);
    BY_HANDLE_FILE_INFORMATION info{};
    if (count && count < std::size(name) && ::GetFileInformationByHandle(file, &info) && SamePath(name, boundary.target)) {
      const bool directory = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
      if (directory == (boundary.fault == Fault::DirectoryTimestamp)) {
        ++boundary.matchingTimestampCalls;
        if (boundary.denied == 0) { ++boundary.denied; ::SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
      }
    }
  }
  return ::SetFileTime(file, creation, access, write);
}
}

int wmain(int argc, wchar_t** argv) {
  if (argc != 4 || std::wstring(argv[1]) != L"--headless" ||
      (std::wstring(argv[2]) != L"--before-root" && std::wstring(argv[2]) != L"--after")) {
    std::cerr << "Use --headless --before-root|--after with a fresh compile-bound fixture child.\n"; return 2;
  }
  const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(com)) return 3;
  fs::path fixture;
  try {
    _7zSetHInstance(GetModuleHandleW(nullptr));
    fixture = ReserveFixture(argv[3]);
    std::wcout << L"FIXTURE=" << fixture.wstring() << L"\n";
    const bool before = std::wstring(argv[2]) == L"--before-root";
    RootCase(fixture / L"r", before);
    TimestampCase(fixture / L"f", false);
    TimestampCase(fixture / L"d", true);
    std::cout << "REGRESSION_PASSED=" << (before ? 0 : 1) << "\nPRODUCTION_ITEM_CAP=500000\n"
              << "CHECKS=" << checks << "\nCASES=3\nUNEXPECTED_FAILURES=0\n";
    CoUninitialize(); return 0;
  } catch (const std::exception& error) {
    boundary = {};
    std::cerr << "UNEXPECTED_FAILURE=" << error.what() << "\nREGRESSION_PASSED=0\n";
    if (!fixture.empty()) std::wcerr << L"RETAINED_FIXTURE=" << fixture.wstring() << L"\n";
    CoUninitialize(); return 1;
  }
}
