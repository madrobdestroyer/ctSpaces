// Ordinary valid-backup item-count boundary regression using a guarded cap of eight. No UI, child processes,
// external profiles, malformed archives, deletion, or existing-fixture reuse.
#include "../BackupEngine.h"
#include "../version.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
#if !defined(CTSPACES_BACKUP_TEST_HOOKS) || !defined(CTSPACES_ARCHIVE_ITEM_LIMIT_FOR_TEST)
#error This regression requires both explicitly guarded test definitions.
#endif
#ifndef CTSPACES_ITEM_LIMIT_FIXTURE_PARENT
#error The wrapper must bind a fresh workspace artifact directory through its generated header.
#endif
static_assert(CTSPACES_ARCHIVE_ITEM_LIMIT_FOR_TEST == 8);
static_assert(CtArchiveSafety::kMaxItems == 8,
              "The shared test-only archive-item cap must be active in all units.");
HRESULT CtBackupTestCreateGuid(GUID* identity) { return CoCreateGuid(identity); }

namespace {
unsigned checks = 0;
void Check(bool ok, const char* text) {
  ++checks;
  std::cout << (ok ? "PASS " : "FAIL ") << text << std::endl;
  if (!ok) throw std::runtime_error(text);
}
std::string Read(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Private fixture file cannot be read.");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void WriteNew(const fs::path& path, const std::string& bytes) {
  fs::create_directories(path.parent_path());
  HANDLE output = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (output == INVALID_HANDLE_VALUE)
    throw std::runtime_error("Private fixture file already exists or is unavailable.");
  DWORD written = 0;
  const bool ok = WriteFile(output, bytes.data(), static_cast<DWORD>(bytes.size()),
                            &written, nullptr) && written == static_cast<DWORD>(bytes.size()) &&
                  FlushFileBuffers(output);
  CloseHandle(output);
  if (!ok) throw std::runtime_error("Private fixture write failed.");
}
void Status(const wchar_t* key, const CtBackup::Result& result) {
  std::wcout << key << L"=0x" << std::hex << std::setw(8) << std::setfill(L'0')
             << static_cast<unsigned long>(result.code) << std::dec
             << L"\nDETAILS=" << result.details << L"\n";
}
void RequireNoReparseAncestors(const fs::path& path) {
  for (fs::path current = path; !current.empty();) {
    const DWORD attributes = GetFileAttributesW(current.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
      throw std::runtime_error("A fixture path or ancestor is a reparse point.");
    const auto parent = current.parent_path();
    if (parent == current) break;
    current = parent;
  }
}
fs::path ReserveFixture(const wchar_t* requested) {
  const fs::path evidence = fs::path(CTSPACES_ITEM_LIMIT_FIXTURE_PARENT).lexically_normal();
  if (!evidence.is_absolute())
    throw std::runtime_error("Compile-bound artifact directory must be absolute.");
  const fs::path raw(requested);
  if (!raw.is_absolute() || !raw.has_root_name())
    throw std::runtime_error("An absolute private fixture path is required.");
  for (const auto& part : raw)
    if (part == L"." || part == L"..")
      throw std::runtime_error("Fixture dot components are refused.");
  const auto fixture = raw.lexically_normal();
  // One fresh, short direct child keeps final target paths within app budgets.
  if (_wcsicmp(fixture.parent_path().c_str(), evidence.c_str()) != 0 ||
      fixture.filename().empty() || fixture.filename().native().size() > 12)
    throw std::runtime_error("Fixture must be a fresh short direct child of the compile-bound artifact directory.");
  RequireNoReparseAncestors(fixture);
  if (GetFileAttributesW(fixture.c_str()) != INVALID_FILE_ATTRIBUTES ||
      !CreateDirectoryW(fixture.c_str(), nullptr))
    throw std::runtime_error("Existing or unavailable fixture refused; retained data is never reused.");
  RequireNoReparseAncestors(fixture);
  return fixture;
}
struct Entry {
  bool directory = false;
  DWORD volume = 0, high = 0, low = 0;
  std::string bytes;
  bool operator==(const Entry&) const = default;
};
struct Tree {
  bool exists = false;
  std::map<std::wstring, Entry> entries;
  bool operator==(const Tree&) const = default;
};
Entry CaptureEntry(const fs::path& path) {
  RequireNoReparseAncestors(path);
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES)
    throw std::runtime_error("Private inventory entry disappeared.");
  Entry entry;
  entry.directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT |
          (entry.directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr);
  if (file == INVALID_HANDLE_VALUE)
    throw std::runtime_error("Private inventory object could not be bound.");
  BY_HANDLE_FILE_INFORMATION info{};
  const bool ok = GetFileInformationByHandle(file, &info) &&
      !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
      (entry.directory || info.nNumberOfLinks == 1) && GetFileType(file) == FILE_TYPE_DISK;
  CloseHandle(file);
  if (!ok) throw std::runtime_error("Private inventory refused an unsafe filesystem object.");
  entry.volume = info.dwVolumeSerialNumber;
  entry.high = info.nFileIndexHigh;
  entry.low = info.nFileIndexLow;
  if (!entry.directory) entry.bytes = Read(path);
  return entry;
}
Tree Snapshot(const fs::path& root) {
  RequireNoReparseAncestors(root);
  const DWORD attributes = GetFileAttributesW(root.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    const DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return {};
    throw std::runtime_error("Private inventory root could not be inspected.");
  }
  Tree result;
  result.exists = true;
  result.entries.emplace(L"", CaptureEntry(root));
  if (attributes & FILE_ATTRIBUTE_DIRECTORY)
    for (const auto& child : fs::recursive_directory_iterator(root))
      result.entries.emplace(child.path().lexically_relative(root).native(), CaptureEntry(child.path()));
  return result;
}
bool ExactFiveFiles(const fs::path& alpha) {
  const auto tree = Snapshot(alpha);
  if (!tree.exists || tree.entries.size() != 6 || !tree.entries.at(L"").directory) return false;
  for (unsigned index = 0; index < 5; ++index) {
    const std::wstring name = L"file" + std::to_wstring(index) + L".txt";
    const auto item = tree.entries.find(name);
    if (item == tree.entries.end() || item->second.directory ||
        item->second.bytes != "ordinary tiny client file " + std::to_string(index) + "\r\n")
      return false;
  }
  return true;
}
}

int wmain(int argc, wchar_t** argv) {
  if (argc != 4 || std::wstring(argv[1]) != L"--headless" ||
      (std::wstring(argv[2]) != L"--before" && std::wstring(argv[2]) != L"--after")) {
    std::cerr << "Use --headless --before|--after with a fresh short private fixture child.\n";
    return 2;
  }
  const bool before = std::wstring(argv[2]) == L"--before";
  fs::path fixture;
  const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(com)) return 3;
  _7zSetHInstance(GetModuleHandleW(nullptr));
  try {
    fixture = ReserveFixture(argv[3]);
    std::wcout << L"FIXTURE=" << fixture.wstring() << L"\n";
    const fs::path source = fixture / L"s" / L"Sites";
    const fs::path alpha = source / L"Alpha";
    const fs::path archive = fixture / L"o" / L"backup.7z";
    const fs::path stage = fixture / L"t";
    const fs::path live = fixture / L"l" / L"Sites";
    const fs::path recovery = fixture / L"l" / L"R";
    fs::create_directories(archive.parent_path());
    for (unsigned index = 0; index < 5; ++index)
      WriteNew(alpha / (L"file" + std::to_wstring(index) + L".txt"),
               "ordinary tiny client file " + std::to_string(index) + "\r\n");
    WriteNew(live / L"Previous" / L"old.txt", "original private live notebook/browser sentinel\r\n");
    Check(CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(live, L"Alpha"),
          "Final client target fits the unchanged managed-client path budget");
    const auto sourceBefore = Snapshot(source), liveBefore = Snapshot(live), recoveryBefore = Snapshot(recovery);
    const auto created = CtBackup::CreateValidated7zBackup(source, archive, CTSPACES_VERSION_TEXT, nullptr, nullptr);
    Status(L"AT_LIMIT_BACKUP_HRESULT", created);
    Check(created.ok(), "Production backend creates the ordinary at-limit archive");
    Check(Snapshot(source) == sourceBefore, "At-limit backup preserves source bytes and object identities");
    _7zArchiveInfo info{};
    Check(_7zInspect7z(archive.c_str(), &info) == S_OK && info.itemCount == 8 &&
          info.fileCount == 6 && info.directoryCount == 2 && info.sitesProfileCount == 1,
          "Actual SDK inspection confirms Sites, Alpha, five files and manifest: eight entries");
    Check(_7zTest7z(archive.c_str(), nullptr, nullptr) == S_OK,
          "The ordinary at-limit archive passes an actual full CRC test");
    const auto archiveBefore = Snapshot(archive);
    CtBackup::StagedRestore staged;
    const auto stagedResult = CtBackup::StageValidated7zRestore(archive, stage, nullptr, nullptr, staged);
    Status(L"AT_LIMIT_STAGE_HRESULT", stagedResult);
    Check(Snapshot(source) == sourceBefore && Snapshot(live) == liveBefore &&
          Snapshot(recovery) == recoveryBefore && Snapshot(archive) == archiveBefore,
          "Staging leaves source, active collection, recovery namespace and archive intact");
    if (before) {
      Check(!stagedResult.ok() && stagedResult.code == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) &&
            stagedResult.details.find(L"staged restore tree exceeds") != std::wstring::npos,
            "Before-fix run reproduces the valid-archive seal item-limit refusal");
      std::cout << "EXPECTED_DEFECT_REPRODUCED=1\nREGRESSION_PASSED=0\n";
    } else {
      Check(stagedResult.ok(), "At-limit ordinary backup stages successfully after the fix");
      Check(Snapshot(stage).entries.size() == 9,
            "The identity seal covers eight archive entries plus its staging root");
      Check(staged.profileCount == 1 && ExactFiveFiles(staged.payloadDir / L"Alpha"),
            "Staged payload retains the exact intended client and all five files");
      _7zArchiveInfo precommitInfo{};
      Check(Snapshot(archive) == archiveBefore &&
            _7zInspect7z(archive.c_str(), &precommitInfo) == S_OK && precommitInfo.itemCount == 8 &&
            _7zTest7z(archive.c_str(), nullptr, nullptr) == S_OK,
            "Before commit the paired original archive still has eight entries and passes full CRC");
      const auto committed = CtBackup::CommitStagedRestore(staged, live, recovery);
      Status(L"AT_LIMIT_COMMIT_HRESULT", committed);
      Check(committed.ok() && ExactFiveFiles(live / L"Alpha") && !fs::exists(live / L"Previous"),
            "At-limit restored collection commits with exact selected payload");
      Check(Snapshot(recovery) == liveBefore,
            "Successful commit preserves the original live collection and identities in recovery");
      Check(Snapshot(source) == sourceBefore && Snapshot(archive) == archiveBefore && !fs::exists(stage),
            "Successful commit preserves source/archive and removes only its owned empty stage");
    }
    // A sixth ordinary file means nine archive entries. Never create a malformed archive.
    WriteNew(alpha / L"file5.txt", "ordinary sixth file exceeds the eight-entry test cap\r\n");
    const auto sourceSix = Snapshot(source), liveSix = Snapshot(live), recoverySix = Snapshot(recovery);
    const auto refused = CtBackup::CreateValidated7zBackup(source, archive, CTSPACES_VERSION_TEXT, nullptr, nullptr);
    Status(L"OVER_LIMIT_BACKUP_HRESULT", refused);
    Check(!refused.ok(), "The ninth archive entry is refused without publishing a backup");
    Check(Snapshot(archive) == archiveBefore && Snapshot(source) == sourceSix &&
          Snapshot(live) == liveSix && Snapshot(recovery) == recoverySix,
          "Over-limit backup preserves the existing archive, source, live collection and recovery exactly");
    _7zArchiveInfo retainedInfo{};
    Check(_7zInspect7z(archive.c_str(), &retainedInfo) == S_OK && retainedInfo.itemCount == 8 &&
          _7zTest7z(archive.c_str(), nullptr, nullptr) == S_OK,
          "Refused replacement leaves the existing eight-entry archive inspectable and CRC-valid");
    if (!before) std::cout << "REGRESSION_PASSED=1\n";
    std::cout << "TEST_ITEM_CAP=8\nPRODUCTION_ITEM_CAP=500000\nPRODUCTION_SCALE_FIXTURE=0\n"
              << "MODE=" << (before ? "before" : "after") << "\nCHECKS=" << checks
              << "\nUNEXPECTED_FAILURES=0\n";
    CoUninitialize();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "UNEXPECTED_FAILURE=" << error.what() << "\nREGRESSION_PASSED=0\n";
    if (!fixture.empty()) std::wcerr << L"RETAINED_FIXTURE=" << fixture.wstring() << L"\n";
    CoUninitialize();
    return 1;
  }
}
