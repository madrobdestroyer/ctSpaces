#undef CreateFileW
#undef WriteFile
#undef ReadFile
#undef FlushFileBuffers
#undef SetFileInformationByHandle
#include "../BackupEngine.h"
#include "../version.h"
#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;
namespace {
enum class Fault { None, ManifestWrite, ManifestFlush, ArchiveWrite, ArchiveRead,
                   ArchiveFlush, PublishDiskFull, PublishSharing, CleanupFailure,
                   GuardRefusal, GuardException, RollbackFailure };
struct BoundaryState {
  Fault fault = Fault::None;
  fs::path output, manifest, archive;
  BY_HANDLE_FILE_INFORMATION manifestIdentity{}, archiveIdentity{};
  bool hasManifest = false, hasArchive = false, crc = false, hit = false;
  unsigned renameCalls = 0;
} g_state;
unsigned g_checks = 0;
const std::vector<unsigned char> kLive{'O','R','I','G','I','N','A','L',' ','L','I','V','E'};

void Check(bool value, const char *text) {
  ++g_checks;
  if (!value) throw std::runtime_error(text);
}
std::vector<unsigned char> Read(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("fixture read failed");
  return {std::istreambuf_iterator<char>(in), {}};
}
void Write(const fs::path &path, const std::vector<unsigned char> &bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
  if (!out) throw std::runtime_error("fixture write failed");
}
bool Identity(HANDLE file, const BY_HANDLE_FILE_INFORMATION &expected) {
  BY_HANDLE_FILE_INFORMATION actual{};
  return ::GetFileInformationByHandle(file, &actual) &&
      actual.dwVolumeSerialNumber == expected.dwVolumeSerialNumber &&
      actual.nFileIndexHigh == expected.nFileIndexHigh &&
      actual.nFileIndexLow == expected.nFileIndexLow;
}
bool IsManifest(HANDLE file) { return g_state.hasManifest && Identity(file, g_state.manifestIdentity); }
bool IsArchive(HANDLE file) { return g_state.hasArchive && Identity(file, g_state.archiveIdentity); }

void PublicationBoundary(const fs::path &, const fs::path &,
    CtBackup::BackupPublicationBoundary boundary, void *) {
  if (boundary == CtBackup::BackupPublicationBoundary::ArchiveCreated)
    g_state.crc = true;
}
bool Guard(void *, std::wstring &details) {
  g_state.hit = true;
  if (g_state.fault == Fault::GuardException)
    throw std::runtime_error("Scoped final safety guard interruption");
  details = L"Scoped final safety guard declined restore";
  return false;
}

void TestBackupFault(const fs::path &fixture, Fault fault,
                     const std::vector<unsigned char> &sourceBytes) {
  const auto sites = fixture / L"d/Sites", output = fixture / L"o";
  fs::create_directories(sites / L"Alpha"); fs::create_directory(output);
  Write(sites / L"Alpha/state.bin", sourceBytes);
  const auto selected = output / L"previous.7z";
  g_state = {};
  Check(CtBackup::CreateValidated7zBackup(sites, selected, CTSPACES_VERSION_TEXT,
      nullptr, nullptr).ok(), "baseline actual SDK backup creation failed");
  const auto prior = Read(selected);
  g_state = {}; g_state.output = output; g_state.fault = fault;
  CtBackup::SetBackupPublicationHookForTest(PublicationBoundary, nullptr);
  const auto result = CtBackup::CreateValidated7zBackup(sites, selected,
      CTSPACES_VERSION_TEXT, nullptr, nullptr);
  CtBackup::SetBackupPublicationHookForTest(nullptr, nullptr);
  std::wcout << L"BACKUP_CASE=" << static_cast<unsigned>(fault)
      << L" HIT=" << g_state.hit << L" RESULT=" << std::hex << result.code
      << std::dec << L" DETAILS=" << result.details << L'\n';
  Check(g_state.hit, "requested actual native backup fault was not reached");
  Check(!result.ok(), "native disk/verification fault was incorrectly successful");
  Check(Read(selected) == prior, "failed backup changed exact previous archive bytes");
  Check(Read(sites / L"Alpha/state.bin") == sourceBytes, "backup fault changed source bytes");
  if (fault == Fault::CleanupFailure) {
    Check(fs::is_regular_file(g_state.archive), "refused cleanup did not retain owned partial archive");
    Check(result.details.find(g_state.archive.wstring()) != std::wstring::npos,
          "refused cleanup did not report exact retained archive path");
    Check(std::distance(fs::directory_iterator(output), fs::directory_iterator{}) == 2,
          "refused archive cleanup left unexpected manifest or missing prior output");
  } else {
    Check(std::distance(fs::directory_iterator(output), fs::directory_iterator{}) == 1,
          "failed backup leaked an owned staging archive or manifest");
  }
  g_state.fault = Fault::None;
  Check(SUCCEEDED(_7zTest7z(selected.c_str(), nullptr, nullptr)),
        "previous archive was not SDK-valid after rejected backup");
  Check(CtBackup::CreateValidated7zBackup(sites, selected, CTSPACES_VERSION_TEXT,
      nullptr, nullptr).ok(), "backup did not recover after native fault removal");
}

void TestRestoreFault(const fs::path &fixture, Fault fault,
                      const std::vector<unsigned char> &sourceBytes) {
  const auto sites = fixture / L"d/Sites", archive = fixture / L"a.7z";
  const auto live = fixture / L"l/Sites", recovery = fixture / L"r";
  fs::create_directories(sites / L"Alpha"); fs::create_directories(live / L"Keep");
  Write(sites / L"Alpha/state.bin", sourceBytes); Write(live / L"Keep/data.bin", kLive);
  g_state = {};
  Check(CtBackup::CreateValidated7zBackup(sites, archive, CTSPACES_VERSION_TEXT,
      nullptr, nullptr).ok(), "restore failure fixture backup failed");
  const auto originalArchive = Read(archive);
  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"s", nullptr,
      nullptr, staged).ok(), "restore failure fixture staging failed");
  g_state = {}; g_state.fault = fault;
  const auto result = CtBackup::CommitStagedRestore(staged, live, recovery,
      fault == Fault::RollbackFailure ? nullptr : Guard, nullptr);
  std::wcout << L"RESTORE_CASE=" << static_cast<unsigned>(fault)
      << L" HIT=" << g_state.hit << L" RESULT=" << std::hex << result.code
      << std::dec << L" DETAILS=" << result.details << L'\n';
  Check(g_state.hit && !result.ok(), "restore interruption was not rejected");
  Check(Read(archive) == originalArchive, "restore interruption changed source archive bytes");
  Check(Read(staged.payloadDir / L"Alpha/state.bin") == sourceBytes,
        "restore interruption changed complete verified payload bytes");
  if (fault == Fault::RollbackFailure) {
    Check(!fs::exists(live) && Read(recovery / L"Keep/data.bin") == kLive,
          "failed rollback did not retain exact old live bytes in recovery");
    Check(result.details.find(L"original Sites could not be returned") != std::wstring::npos,
          "failed rollback omitted recovery failure details");
    g_state.fault = Fault::None;
    // Explicit synthetic operator recovery; do not hide it as an automatic retry.
    Check(::MoveFileExW(recovery.c_str(), live.c_str(), 0) != FALSE,
          "synthetic original-data recovery failed");
  } else {
    Check(Read(live / L"Keep/data.bin") == kLive && !fs::exists(recovery),
          "declined/interrupted guard changed the original live namespace");
    g_state.fault = Fault::None;
  }
  Check(CtBackup::CommitStagedRestore(staged, live, recovery).ok(),
        "preserved payload could not commit after interruption removal");
  Check(Read(live / L"Alpha/state.bin") == sourceBytes &&
        Read(recovery / L"Keep/data.bin") == kLive,
        "retry did not preserve exact selected and original live bytes");
}
} // namespace

HRESULT CtBackupTestCreateGuid(GUID *identity) { return CoCreateGuid(identity); }

namespace backup_recovery_boundary {
HANDLE WINAPI CreateFileW(LPCWSTR name, DWORD access, DWORD sharing,
    LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD flags, HANDLE templ) {
  HANDLE file = ::CreateFileW(name, access, sharing, security, creation, flags, templ);
  if (file != INVALID_HANDLE_VALUE && name && creation == CREATE_NEW &&
      !g_state.output.empty() && fs::path(name).parent_path() == g_state.output) {
    if ((access & DELETE) != 0) {
      g_state.archive = name;
      g_state.hasArchive = ::GetFileInformationByHandle(file, &g_state.archiveIdentity) != FALSE;
    } else {
      g_state.manifest = name;
      g_state.hasManifest = ::GetFileInformationByHandle(file, &g_state.manifestIdentity) != FALSE;
    }
  }
  return file;
}
BOOL WINAPI WriteFile(HANDLE file, LPCVOID data, DWORD count, LPDWORD written,
                      LPOVERLAPPED overlapped) {
  if (!g_state.hit && ((g_state.fault == Fault::ManifestWrite && IsManifest(file)) ||
      ((g_state.fault == Fault::ArchiveWrite || g_state.fault == Fault::CleanupFailure) && IsArchive(file)))) {
    const DWORD partial = (std::min)(count, DWORD{17});
    if (partial && !::WriteFile(file, data, partial, written, overlapped)) return FALSE;
    g_state.hit = true; SetLastError(ERROR_DISK_FULL); return FALSE;
  }
  return ::WriteFile(file, data, count, written, overlapped);
}
BOOL WINAPI ReadFile(HANDLE file, LPVOID data, DWORD count, LPDWORD read,
                     LPOVERLAPPED overlapped) {
  if (g_state.fault == Fault::ArchiveRead && g_state.crc && IsArchive(file) && !g_state.hit) {
    g_state.hit = true; if (read) *read = 0; SetLastError(ERROR_CRC); return FALSE;
  }
  return ::ReadFile(file, data, count, read, overlapped);
}
BOOL WINAPI FlushFileBuffers(HANDLE file) {
  if (!g_state.hit && ((g_state.fault == Fault::ManifestFlush && IsManifest(file)) ||
      (g_state.fault == Fault::ArchiveFlush && g_state.crc && IsArchive(file)))) {
    g_state.hit = true; SetLastError(ERROR_WRITE_FAULT); return FALSE;
  }
  return ::FlushFileBuffers(file);
}
BOOL WINAPI SetFileInformationByHandle(HANDLE file, FILE_INFO_BY_HANDLE_CLASS kind,
                                      LPVOID info, DWORD bytes) {
  if (kind == FileRenameInfo) {
    if ((g_state.fault == Fault::PublishDiskFull || g_state.fault == Fault::PublishSharing) && IsArchive(file)) {
      g_state.hit = true; SetLastError(g_state.fault == Fault::PublishDiskFull ? ERROR_DISK_FULL : ERROR_SHARING_VIOLATION); return FALSE;
    }
    if (g_state.fault == Fault::RollbackFailure && ++g_state.renameCalls >= 2) {
      g_state.hit = true; SetLastError(ERROR_DISK_FULL); return FALSE;
    }
  }
  if (kind == FileDispositionInfo && g_state.fault == Fault::CleanupFailure && IsArchive(file)) {
    SetLastError(ERROR_ACCESS_DENIED); return FALSE;
  }
  return ::SetFileInformationByHandle(file, kind, info, bytes);
}
} // namespace backup_recovery_boundary

int wmain(int argc, wchar_t **) {
  try {
    Check(argc == 1, "unknown arguments refused");
    GUID id{}; Check(SUCCEEDED(CoCreateGuid(&id)), "unique fixture GUID failed");
    const auto root = fs::current_path() / L"build" /
        std::format(L"br7-{}-{}", GetCurrentProcessId(), GetTickCount64());
    Check(fs::create_directory(root), "unique backup recovery fixture failed");
    std::vector<unsigned char> source(32768);
    unsigned random = 0x7135A29Bu;
    for (auto &byte : source) { random = random * 1664525u + 1013904223u; byte = static_cast<unsigned char>(random >> 24); }
    for (const auto fault : {Fault::ManifestWrite, Fault::ManifestFlush,
         Fault::ArchiveWrite, Fault::ArchiveRead, Fault::ArchiveFlush,
         Fault::PublishDiskFull, Fault::PublishSharing, Fault::CleanupFailure})
      TestBackupFault(root / std::to_wstring(static_cast<unsigned>(fault)), fault, source);
    for (const auto fault : {Fault::GuardRefusal, Fault::GuardException, Fault::RollbackFailure})
      TestRestoreFault(root / std::to_wstring(static_cast<unsigned>(fault)), fault, source);
    std::cout << "Backup recovery fault checks passed: " << g_checks << " (11 scenarios; seed0x7135A29B).\n";
    std::wcout << L"FIXTURE=" << root << L'\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Backup recovery check failed: " << error.what() << '\n';
    return 1;
  }
}
