// Ordinary valid-backup lifecycle regression. No GUI, malformed archives,
// external profiles, native fault injection, or recursive fixture cleanup.
#include "../BackupEngine.h"
#include "../version.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>

#ifndef CTSPACES_RESTORE_LIFECYCLE_FIXTURE_PARENT
#error Bind this test to its new workspace artifact directory through its wrapper.
#endif
#ifdef CTSPACES_ARCHIVE_ITEM_LIMIT_FOR_TEST
#error This regression requires the ordinary production archive limits.
#endif
static_assert(CtArchiveSafety::kMaxItems == 500000);
namespace fs = std::filesystem;
namespace {
constexpr unsigned long long kFixtureBudget = 2ull * 1024ull * 1024ull;
unsigned checks = 0, cases = 0;
unsigned terminalContainmentFailures = 0, terminalEscapes = 0, terminalHits = 0;

void Check(bool ok, const char* description) {
  ++checks;
  std::cout << (ok ? "PASS " : "FAIL ") << description << std::endl;
  if (!ok) throw std::runtime_error(description);
}
void NoReparseAncestors(const fs::path& path) {
  for (auto current = path; !current.empty();) {
    const auto attributes = GetFileAttributesW(current.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
      throw std::runtime_error("Reparse fixture path or ancestor refused.");
    const auto parent = current.parent_path();
    if (parent == current) break;
    current = parent;
  }
}
fs::path ReserveFixture(const wchar_t* requested) {
  const auto parent = fs::path(CTSPACES_RESTORE_LIFECYCLE_FIXTURE_PARENT).lexically_normal();
  const fs::path raw(requested);
  if (!parent.is_absolute() || !raw.is_absolute()) throw std::runtime_error("Absolute fixture paths required.");
  for (const auto& part : raw)
    if (part == L"." || part == L"..") throw std::runtime_error("Fixture dot components refused.");
  const auto root = raw.lexically_normal();
  if (_wcsicmp(root.parent_path().c_str(), parent.c_str()) || root.filename().empty() || root.filename().native().size() > 12)
    throw std::runtime_error("Fixture must be a fresh short direct child of its compile-bound evidence directory.");
  NoReparseAncestors(root);
  if (GetFileAttributesW(root.c_str()) != INVALID_FILE_ATTRIBUTES || !CreateDirectoryW(root.c_str(), nullptr))
    throw std::runtime_error("Existing or unavailable fixture refused; evidence is never reused.");
  NoReparseAncestors(root);
  return root;
}
void WriteNew(const fs::path& path, const std::string& bytes) {
  if (bytes.size() > 8192) throw std::runtime_error("Each ordinary fixture file is limited to eight KiB.");
  NoReparseAncestors(path);
  fs::create_directories(path.parent_path());
  NoReparseAncestors(path);
  HANDLE output = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (output == INVALID_HANDLE_VALUE) throw std::runtime_error("Private CREATE_NEW failed.");
  DWORD wrote = 0;
  const bool ok = WriteFile(output, bytes.data(), static_cast<DWORD>(bytes.size()), &wrote, nullptr) &&
      wrote == bytes.size() && FlushFileBuffers(output);
  CloseHandle(output);
  if (!ok) throw std::runtime_error("Private fixture write failed.");
}
std::string Read(const fs::path& path) {
  if (fs::file_size(path) > kFixtureBudget) throw std::runtime_error("Oversized private fixture refused.");
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("Private fixture read failed.");
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
unsigned long long Time(FILETIME value) {
  return (static_cast<unsigned long long>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}
struct Entry {
  bool directory = false;
  DWORD volume = 0, high = 0, low = 0, attributes = 0;
  unsigned long long creation = 0, write = 0;
  std::string bytes;
  bool operator==(const Entry&) const = default;
};
struct Tree {
  bool exists = false;
  std::map<std::wstring, Entry> entries;
  bool operator==(const Tree&) const = default;
};
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
  Entry entry;
  entry.directory = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  entry.volume = info.dwVolumeSerialNumber; entry.high = info.nFileIndexHigh; entry.low = info.nFileIndexLow;
  entry.attributes = info.dwFileAttributes;
  entry.creation = Time(info.ftCreationTime); entry.write = Time(info.ftLastWriteTime);
  if (!entry.directory) entry.bytes = Read(path);
  return entry;
}
Tree Snapshot(const fs::path& path) {
  NoReparseAncestors(path);
  const auto attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    const auto error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return {};
    throw std::runtime_error("Private snapshot root unavailable.");
  }
  Tree tree;
  tree.exists = true;
  tree.entries.emplace(L"", Capture(path));
  if (attributes & FILE_ATTRIBUTE_DIRECTORY)
    for (const auto& child : fs::recursive_directory_iterator(path)) {
      // Refuse the link at its own entry before the iterator can advance into it.
      tree.entries.emplace(child.path().lexically_relative(path).native(), Capture(child.path()));
    }
  return tree;
}
bool SamePayload(const Tree& left, const Tree& right) {
  if (!left.exists || !right.exists || left.entries.size() != right.entries.size()) return false;
  for (const auto& [name, entry] : left.entries) {
    const auto found = right.entries.find(name);
    if (found == right.entries.end() || entry.directory != found->second.directory ||
        entry.bytes != found->second.bytes || entry.creation != found->second.creation ||
        entry.write != found->second.write) return false;
  }
  return true;
}
unsigned long long PhysicalBytes(const fs::path& root) {
  const auto tree = Snapshot(root);
  unsigned long long bytes = 0;
  for (const auto& [name, entry] : tree.entries) {
    (void)name;
    if (entry.bytes.size() > kFixtureBudget - bytes) throw std::runtime_error("Private fixture exceeds its two MiB budget.");
    bytes += entry.bytes.size();
  }
  return bytes;
}
bool Cleared(const CtBackup::StagedRestore& staged) {
  return staged.stagingRoot.empty() && staged.payloadDir.empty() && staged.profileCount == 0 && !staged.validation;
}
void Status(const wchar_t* name, const CtBackup::Result& result) {
  std::wcout << name << L"=0x" << std::hex << std::setw(8) << std::setfill(L'0')
             << static_cast<unsigned long>(result.code) << std::dec << L"\nDETAILS=" << result.details << L'\n';
}
struct ProgressAbort {
  bool atCompletion = false;
  std::atomic<unsigned> reports{0};
  std::atomic<bool> fired{false};
};
void ThrowProgress(void* user, _7zOp operation, unsigned percent, const wchar_t*) {
  auto& state = *static_cast<ProgressAbort*>(user);
  if (operation != _7zOp::Extract) return;
  state.reports.fetch_add(1);
  if ((!state.atCompletion || percent == 100) && !state.fired.exchange(true))
    throw std::runtime_error("Scoped ordinary valid-backup progress interruption.");
}

void Run(const fs::path& root) {
  const auto source = root / L"s/Sites";
  const auto archive = root / L"o/b.7z";
  const auto live = root / L"l/Sites";
  const auto recovery = root / L"l/R";
  const auto nextRecovery = root / L"l/R2";
  fs::create_directories(archive.parent_path());
  std::string payload(8192, '\0');
  unsigned random = 0x635030u;
  for (auto& value : payload) { random ^= random << 13; random ^= random >> 17; random ^= random << 5; value = static_cast<char>(random >> 8); }
  WriteNew(source / L"Alpha/state.bin", payload);
  WriteNew(source / L"Alpha/ctSpaces-client-notes.txt", "Alpha legacy note: independently owned content\r\n");
  WriteNew(source / L"Beta/ctSpaces-client-notes.txt", "Beta legacy note: different independently owned content\r\n");
  fs::create_directory(source / L"Beta/empty");
  WriteNew(live / L"Previous/ctSpaces-client-notes.txt", "Previous live collection must remain recoverable\r\n");
  const auto sourceBefore = Snapshot(source);
  const auto liveBefore = Snapshot(live);
  Check(CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(live, L"Alpha") &&
        CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(live, L"Beta"),
        "Both final client roots fit the production path budget");
  const auto backedUp = CtBackup::CreateValidated7zBackup(source, archive, CTSPACES_VERSION_TEXT, nullptr, nullptr);
  Status(L"ORDINARY_BACKUP", backedUp);
  Check(backedUp.ok(), "The actual backend and SDK create the small ordinary two-client backup");
  _7zArchiveInfo info{};
  Check(_7zInspect7z(archive.c_str(), &info) == S_OK && info.sitesProfileCount == 2 &&
        _7zTest7z(archive.c_str(), nullptr, nullptr) == S_OK,
        "The ordinary archive has two clients and passes full SDK CRC verification");
  const auto archiveBefore = Snapshot(archive);
  Check(Snapshot(source) == sourceBefore && !Snapshot(recovery).exists,
        "Backup leaves source identities/content intact and creates no recovery directory");

  const auto seedPath = root / L"seed";
  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(archive, seedPath, nullptr, nullptr, staged).ok() && staged.validation,
        "A successful initial stage owns its real validation seal");
  Check(staged.profileCount == 2 && SamePayload(sourceBefore, Snapshot(staged.payloadDir)),
        "The successful stage contains both exact independent client payloads and timestamps");
  const auto seed = staged;
  const auto seedCopy = staged;
  const auto seedBefore = Snapshot(seedPath);
  const auto seedPayloadBefore = Snapshot(staged.payloadDir);

  const auto unchanged = [&]() {
    return Snapshot(source) == sourceBefore && Snapshot(archive) == archiveBefore &&
        Snapshot(live) == liveBefore && !Snapshot(recovery).exists &&
        Snapshot(seedPath) == seedBefore;
  };
  ++cases;
  CtBackup::StagedRestore empty;
  auto result = CtBackup::CommitStagedRestore(empty, live, recovery);
  Check(!result.ok(), "A default unsealed restore object cannot be committed");
  Check(unchanged(), "Default-object rejection preserves source/archive/live/stage identities and recovery absence");
  CtBackup::StagedRestore reconstructed{staged.stagingRoot, staged.payloadDir, staged.profileCount, {}};
  result = CtBackup::CommitStagedRestore(reconstructed, live, recovery);
  Check(!result.ok(), "A three-field reconstruction cannot impersonate a validated stage");
  Check(unchanged(), "Reconstructed-object rejection preserves every independent original and valid stage");

  const auto verifyFailedOutput = [&](const CtBackup::Result& failure, const char* description) {
    Check(!failure.ok() && Cleared(staged), description);
    Check(unchanged(), "Failed output reuse leaves the previous sealed tree and all original objects intact");
    const auto rejected = CtBackup::CommitStagedRestore(staged, live, recovery);
    Check(!rejected.ok(), "Direct commit of the failed staging output is rejected");
    Check(unchanged(), "Direct failed-stage commit preserves source/archive/live/stage bytes and identities");
  };
  ++cases;
  result = CtBackup::StageValidated7zRestore(root / L"missing.7z", root / L"missing-stage", nullptr, nullptr, staged);
  Status(L"MISSING_ARCHIVE_STAGE", result);
  verifyFailedOutput(result, "A missing archive clears a reused output object and its previous validation seal");
  Check(!Snapshot(root / L"missing-stage").exists, "A missing archive creates no staging directory");

  ++cases;
  const auto occupied = root / L"occupied";
  WriteNew(occupied / L"foreign.txt", "Independent occupied-directory sentinel\r\n");
  const auto occupiedBefore = Snapshot(occupied);
  staged = seed;
  result = CtBackup::StageValidated7zRestore(archive, occupied, nullptr, nullptr, staged);
  Status(L"OCCUPIED_STAGE", result);
  verifyFailedOutput(result, "An occupied staging destination clears a reused output without inheriting its prior seal");
  Check(Snapshot(occupied) == occupiedBefore, "Occupied-stage rejection retains the exact independent directory and sentinel identity");

  for (const bool atCompletion : {false, true}) {
    ++cases;
    staged = seed;
    const auto failedRoot = root / (atCompletion ? L"abort100" : L"abort-first");
    ProgressAbort abort;
    abort.atCompletion = atCompletion;
    result = CtBackup::StageValidated7zRestore(archive, failedRoot, ThrowProgress, &abort, staged);
    Status(atCompletion ? L"COMPLETION_PROGRESS_STAGE" : L"FIRST_PROGRESS_STAGE", result);
    Check(abort.fired.load() && abort.reports.load() > 0,
          atCompletion ? "The real valid-archive completion progress callback is interrupted" :
                         "The real valid-archive first progress callback is interrupted");
    verifyFailedOutput(result, "An extraction callback interruption fails staging and clears every reusable output field");
    const auto failedTree = Snapshot(failedRoot);
    Check(!failedTree.exists || (failedTree.entries.size() == 1 && failedTree.entries.begin()->second.directory),
          "The controlled failed extraction leaves no unverified payload files behind");
    Check(PhysicalBytes(root) <= kFixtureBudget, "The complete retained ordinary fixture stays below two MiB");
  }

  ++cases;
  const auto retryRoot = root / L"abort100";
  Check(CtBackup::StageValidated7zRestore(archive, retryRoot, nullptr, nullptr, staged).ok() && staged.validation && staged.profileCount == 2,
        "The same output variable and previous failed completion location support a fresh successful retry");
  const auto retryBefore = Snapshot(retryRoot);
  const auto retryPayloadBefore = Snapshot(staged.payloadDir);
  Check(SamePayload(sourceBefore, retryPayloadBefore) && unchanged(),
        "The fresh retry restores both client payloads without disturbing the independent earlier seal");

  ++cases;
  result = CtBackup::CommitStagedRestore(seed, live, recovery);
  Status(L"RETAINED_SEAL_COMMIT", result);
  Check(result.ok(), "An independently retained legitimate seal commits after the reused output's failures");
  const auto installedFirst = Snapshot(live);
  Check(installedFirst == seedPayloadBefore, "The retained-seal commit promotes the exact validated payload identities and bytes");
  Check(Snapshot(recovery) == liveBefore, "The first commit retains the exact previous live collection in recovery");
  Check(Snapshot(source) == sourceBefore && Snapshot(archive) == archiveBefore && Snapshot(retryRoot) == retryBefore,
        "The retained-seal commit leaves source/archive and the separate retry stage unchanged");
  for (const auto* stale : {&seed, &seedCopy}) {
    result = CtBackup::CommitStagedRestore(*stale, live, nextRecovery);
    Check(!result.ok(), "A successfully consumed stage or its earlier token copy cannot be committed twice");
    Check(Snapshot(live) == installedFirst && Snapshot(recovery) == liveBefore && !Snapshot(nextRecovery).exists &&
          Snapshot(retryRoot) == retryBefore && Snapshot(source) == sourceBefore && Snapshot(archive) == archiveBefore,
          "A stale-token refusal preserves installed clients, first recovery, retry stage and original archive");
  }
  Check(Snapshot(occupied) == occupiedBefore, "The unrelated occupied-stage sentinel remains byte/identity exact through successful and stale commits");

  result = CtBackup::CommitStagedRestore(staged, live, nextRecovery);
  Status(L"FRESH_RETRY_COMMIT", result);
  Check(result.ok(), "The fresh retry's independent stage remains commit-ready after the first token is consumed");
  const auto installedSecond = Snapshot(live);
  Check(installedSecond == retryPayloadBefore, "Fresh retry commit promotes its own exact validated object identities");
  Check(Snapshot(nextRecovery) == installedFirst && Snapshot(recovery) == liveBefore,
        "Sequential commit retains both generations in their intended independent recovery directories");
  const auto staleRecovery = root / L"l/R3";
  result = CtBackup::CommitStagedRestore(staged, live, staleRecovery);
  Check(!result.ok() && !Snapshot(staleRecovery).exists && Snapshot(live) == installedSecond &&
        Snapshot(nextRecovery) == installedFirst && Snapshot(recovery) == liveBefore,
        "The retry token also becomes non-reusable without moving any live or recovery data");
  Check(Snapshot(source) == sourceBefore && Snapshot(archive) == archiveBefore &&
        _7zTest7z(archive.c_str(), nullptr, nullptr) == S_OK,
        "The original source and CRC-valid archive remain exact after the complete lifecycle sequence");
  const auto totalBytes = PhysicalBytes(root);
  Check(totalBytes <= kFixtureBudget, "All retained payload/archive/staging/recovery fixtures fit the explicit two MiB budget");
  std::cout << "BASE_PHYSICAL_FIXTURE_BYTES=" << totalBytes << "\n"
               "ORDINARY_VALID_ARCHIVE=1\nDIRECT_FAILED_STAGE_COMMIT_REJECTED=1\n"
               "REUSED_OUTPUT_CLEARED=1\nFIRST_AND_COMPLETION_PROGRESS_ABORTED=1\n"
               "RETAINED_SEAL_COMMITTED=1\nSTALE_COMMIT_REJECTED=1\n";
}

struct ExactTerminalCallback {
  bool nonStandard = false;
  std::atomic<unsigned> matches{0}, ignoredReports{0};
};
void ThrowExactTerminal(void* user, _7zOp operation, unsigned percent, const wchar_t* item) {
  auto& state = *static_cast<ExactTerminalCallback*>(user);
  // Earlier COM progress can also reach 100 percent. Target only the explicit
  // post-decoder callback, whose item is null, and leave every other report alone.
  if (operation != _7zOp::Extract || percent != 100 || item != nullptr) {
    state.ignoredReports.fetch_add(1);
    return;
  }
  state.matches.fetch_add(1);
  if (state.nonStandard) throw 17;
  throw std::runtime_error("Scoped exact null-item terminal restore callback.");
}
void TerminalContainmentCheck(bool contained, const char* description) {
  ++checks;
  std::cout << (contained ? "PASS " : "FAIL ") << description << std::endl;
  if (!contained) ++terminalContainmentFailures;
}
void ExactTerminalCases(const fs::path& root) {
  const auto source = root / L"s/Sites", archive = root / L"o/b.7z";
  const auto live = root / L"l/Sites", wouldBeRecovery = root / L"l/TerminalRecovery";
  const auto sourceBefore = Snapshot(source), archiveBefore = Snapshot(archive);
  const auto activeBefore = Snapshot(root / L"l");
  CtBackup::StagedRestore retained;
  Check(CtBackup::StageValidated7zRestore(archive, root / L"tkeep", nullptr, nullptr, retained).ok() && retained.validation,
        "An independent ordinary valid stage remains retained across all exact terminal callback probes");
  const auto retainedBefore = Snapshot(retained.stagingRoot);
  const auto unchanged = [&]() {
    return Snapshot(source) == sourceBefore && Snapshot(archive) == archiveBefore &&
        Snapshot(root / L"l") == activeBefore && Snapshot(retained.stagingRoot) == retainedBefore;
  };
  for (const bool stageApi : {true, false}) {
    for (const bool nonStandard : {false, true}) {
      ++cases;
      ExactTerminalCallback callback;
      callback.nonStandard = nonStandard;
      CtBackup::StagedRestore output = retained;
      CtBackup::Result result{E_FAIL, {}};
      bool returned = false, escapedStandard = false, escapedUnknown = false;
      const auto stagePath = root / (nonStandard ? L"tnonstd" : L"tstd");
      try {
        if (stageApi) result = CtBackup::StageValidated7zRestore(archive, stagePath, ThrowExactTerminal, &callback, output);
        else result = {_7zTest7z(archive.c_str(), ThrowExactTerminal, &callback), {}};
        returned = true;
      } catch (const std::exception&) {
        escapedStandard = true;
      } catch (...) {
        // The before proof records this API containment defect without crashing
        // the harness, so cleanup/data ownership can still be independently checked.
        escapedUnknown = true;
      }
      terminalHits += callback.matches.load();
      const bool escaped = escapedStandard || escapedUnknown;
      if (escaped) ++terminalEscapes;
      std::cout << "EXACT_TERMINAL_CASE=" << (stageApi ? "Stage" : "Test7z")
                << '/' << (nonStandard ? "NonStd" : "Std")
                << " MATCHES=" << callback.matches.load()
                << " IGNORED_OTHER_REPORTS=" << callback.ignoredReports.load()
                << " RETURNED=" << returned << " ESCAPED_STD=" << escapedStandard
                << " ESCAPED_UNKNOWN=" << escapedUnknown << '\n';
      if (returned) Status(L"EXACT_TERMINAL_RETURNED_HRESULT", result);
      Check(callback.matches.load() == 1,
            "The callback throws exactly once only for Extract / 100 / null item, after ignoring other progress reports");
      TerminalContainmentCheck(returned && !result.ok() && !escaped,
            "The exact terminal callback exception is contained as a failed HRESULT instead of escaping the API");
      if (stageApi) {
        Check(Cleared(output), "A terminal restore callback failure clears every staging output field and previous seal");
        const auto partial = Snapshot(stagePath);
        Check(!partial.exists || (partial.entries.size() == 1 && partial.entries.begin()->second.directory),
              "Exact terminal callback failure cleans its owned extracted payload without retaining an unverified file");
        Check(!CtBackup::CommitStagedRestore(output, live, wouldBeRecovery).ok(),
              "Direct commit after exact terminal staging failure rejects without creating a recovery collection");
      }
      Check(unchanged(), "Every terminal callback probe preserves source/archive/live/recovery and the independent stage bytes and identities");
      HANDLE exclusive = CreateFileW(archive.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr,
          OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
      const bool released = exclusive != INVALID_HANDLE_VALUE;
      if (released) CloseHandle(exclusive);
      Check(released, "An exact terminal callback exception releases the archive session and its original sharing lease");
    }
  }
  CtBackup::StagedRestore fresh;
  Check(CtBackup::StageValidated7zRestore(archive, root / L"tfresh", nullptr, nullptr, fresh).ok() && fresh.validation &&
        fresh.profileCount == 2 && SamePayload(sourceBefore, Snapshot(fresh.payloadDir)),
        "A fresh no-callback retry stages both exact clients after all raw-terminal exception probes");
  const auto freshBefore = Snapshot(fresh.payloadDir);
  const auto retryLive = root / L"tl/Sites", retryRecovery = root / L"tl/R";
  WriteNew(retryLive / L"Previous/old.txt", "Independent exact-terminal retry recovery data\r\n");
  const auto previous = Snapshot(retryLive);
  Check(CtBackup::CommitStagedRestore(fresh, retryLive, retryRecovery).ok() &&
        Snapshot(retryLive) == freshBefore && Snapshot(retryRecovery) == previous,
        "Fresh terminal-error retry commits its exact object identities and preserves its own previous live collection");
  Check(unchanged() && _7zTest7z(archive.c_str(), nullptr, nullptr) == S_OK,
        "Retry and a clean CRC pass preserve independent originals and restore callback-free operation state");
  const auto bytes = PhysicalBytes(root);
  Check(bytes <= kFixtureBudget, "All retained base and exact-terminal fixtures stay within the two MiB budget");
  std::cout << "PHYSICAL_FIXTURE_BYTES=" << bytes
            << "\nEXACT_TERMINAL_CASES=4\nEXACT_TERMINAL_HITS=" << terminalHits
            << "\nEXACT_TERMINAL_ESCAPES=" << terminalEscapes
            << "\nTERMINAL_CONTAINMENT_FAILURES=" << terminalContainmentFailures
            << "\nEXACT_TERMINAL_OWNERSHIP_PASSED=1\nEXACT_TERMINAL_FRESH_RETRY_PASSED=1\n";
}
} // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc != 3 || wcscmp(argv[1], L"--headless")) return 2;
  try {
    const auto fixture = ReserveFixture(argv[2]);
    Run(fixture);
    if (checks != 51 || cases != 7) throw std::runtime_error("The unchanged base lifecycle scope was not preserved.");
    std::cout << "BASE_CHECKS=" << checks << "\nBASE_CASES=" << cases << '\n';
    ExactTerminalCases(fixture);
    std::cout << "VERSION=" << CTSPACES_VERSION_TEXT << "\nCASES=" << cases
              << "\nCHECKS=" << checks << "\nUNEXPECTED_FAILURES=0\nREGRESSION_PASSED="
              << (terminalContainmentFailures == 0) << '\n';
    return terminalContainmentFailures ? 1 : 0;
  } catch (const std::exception& error) {
    std::cerr << "Restore lifecycle regression failed: " << error.what()
              << "\nCHECKS=" << checks << "\nUNEXPECTED_FAILURES=1\nREGRESSION_PASSED=0\n";
    return 1;
  } catch (...) {
    std::cerr << "Unexpected exception outside the guarded terminal probes.\nUNEXPECTED_FAILURES=1\nREGRESSION_PASSED=0\n";
    return 1;
  }
}
