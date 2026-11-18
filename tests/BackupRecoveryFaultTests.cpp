#undef CreateFileW
#undef WriteFile
#undef ReadFile
#undef FlushFileBuffers
#undef SetFileInformationByHandle
#include "../BackupEngine.h"
#include "../version.h"
#include <algorithm>
#include <atomic>
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
                   GuardRefusal, GuardException, RollbackFailure, TerminalCleanupFailure };
struct BoundaryState {
  Fault fault = Fault::None;
  fs::path output, manifest, archive;
  BY_HANDLE_FILE_INFORMATION manifestIdentity{}, archiveIdentity{};
  bool hasManifest = false, hasArchive = false, crc = false, hit = false;
  unsigned renameCalls = 0;
  unsigned cleanupDenied = 0;
} g_state;
unsigned g_checks = 0;
unsigned g_expectedDefects = 0, g_terminalCases = 0;
bool g_expectBefore = false;
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

struct FileState {
  DWORD volume=0,high=0,low=0,attributes=0;
  FILETIME creation{},write{};
  std::vector<unsigned char> bytes;
};
FileState CaptureFile(const fs::path& path) {
  HANDLE file=::CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Private terminal fixture binding failed");
  BY_HANDLE_FILE_INFORMATION info{};
  const bool good=GetFileInformationByHandle(file,&info)&&GetFileType(file)==FILE_TYPE_DISK&&
      !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&info.nNumberOfLinks==1;
  CloseHandle(file);if(!good)throw std::runtime_error("Unsafe terminal fixture object refused");
  return {info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow,info.dwFileAttributes,
      info.ftCreationTime,info.ftLastWriteTime,Read(path)};
}
bool SameFile(const FileState& left,const FileState& right) {
  return left.volume==right.volume&&left.high==right.high&&left.low==right.low&&left.attributes==right.attributes&&
      CompareFileTime(&left.creation,&right.creation)==0&&CompareFileTime(&left.write,&right.write)==0&&left.bytes==right.bytes;
}
struct TerminalProgress {
  _7zOp operation;
  std::atomic<bool> hit{false};
};
void ThrowTerminal(void* user,_7zOp operation,unsigned percent,const wchar_t* item) {
  auto& terminal=*static_cast<TerminalProgress*>(user);
  if(operation==terminal.operation&&percent==100&&item==nullptr&&!terminal.hit.exchange(true))
    throw std::runtime_error("Scoped terminal legacy progress exception");
}
void ObserveRetention(bool good,const fs::path& retained) {
  ++g_checks;
  if(!good){++g_expectedDefects;std::wcout<<L"EXPECTED_DEFECT_MISSING_RETENTION_PATH="<<retained.wstring()<<L'\n';}
  else std::cout<<"PASS Terminal callback failure reports its exact retained archive path\n";
}
void TestTerminalProgress(const fs::path& fixture,bool verify,bool denyCleanup,
                         const std::vector<unsigned char>& sourceBytes) {
  ++g_terminalCases;
  const auto sites=fixture/L"d/Sites",output=fixture/L"o",selected=output/L"previous.7z";
  fs::create_directories(sites/L"Alpha");fs::create_directory(output);
  Write(sites/L"Alpha/state.bin",sourceBytes);
  g_state={};
  Check(CtBackup::CreateValidated7zBackup(sites,selected,CTSPACES_VERSION_TEXT,nullptr,nullptr).ok(),
      "Terminal fixture starts with a complete actual SDK backup");
  const auto originalArchive=CaptureFile(selected),originalSource=CaptureFile(sites/L"Alpha/state.bin");
  g_state={};g_state.output=output;
  g_state.fault=denyCleanup?Fault::TerminalCleanupFailure:Fault::None;
  TerminalProgress terminal{verify?_7zOp::Extract:_7zOp::Compress};
  const auto result=CtBackup::CreateValidated7zBackup(sites,selected,CTSPACES_VERSION_TEXT,ThrowTerminal,&terminal);
  std::wcout<<L"TERMINAL_CASE="<<(verify?L"verify":L"compress")<<L" CLEANUP_DENIED="<<denyCleanup
      <<L" HIT="<<terminal.hit.load()<<L" RESULT=0x"<<std::hex<<static_cast<unsigned long>(result.code)<<std::dec
      <<L" DETAILS="<<result.details<<L'\n';
  Check(terminal.hit.load()&&!result.ok(),"Exact null-item terminal legacy callback failure is reached and aborts publication");
  Check(result.code==(g_expectBefore?E_FAIL:E_ABORT),"Terminal callback failure has the exact before/after HRESULT for this frozen implementation");
  Check(SameFile(CaptureFile(selected),originalArchive),"Terminal failure preserves old destination bytes, timestamps and identity");
  Check(SameFile(CaptureFile(sites/L"Alpha/state.bin"),originalSource),"Terminal failure preserves source bytes, timestamps and identity");
  Check(SUCCEEDED(_7zTest7z(selected.c_str(),nullptr,nullptr)),"The exact preserved destination remains SDK CRC-valid");
  if(denyCleanup){
    Check(g_state.hasArchive&&g_state.cleanupDenied>0&&fs::is_regular_file(g_state.archive),
        "Exact-identity native cleanup refusal retains the owned complete temporary archive");
    Check(std::distance(fs::directory_iterator(output),fs::directory_iterator{})==2,
        "Refused terminal cleanup retains only old output and its owned archive, with no manifest leak");
    Check(SUCCEEDED(_7zTest7z(g_state.archive.c_str(),nullptr,nullptr)),
        "Retained terminal archive is complete and independently SDK CRC-valid");
    const auto retainedPath=g_state.archive;const auto retained=CaptureFile(retainedPath);
    ObserveRetention(result.details.find(g_state.archive.wstring())!=std::wstring::npos,g_state.archive);
    g_state.fault=Fault::None;
    Check(CtBackup::CreateValidated7zBackup(sites,selected,CTSPACES_VERSION_TEXT,nullptr,nullptr).ok(),
        "A fresh backup succeeds after terminal callback and cleanup failure are removed");
    Check(SameFile(CaptureFile(retainedPath),retained),"Fresh backup never alters the retained complete archive from the failed attempt");
  }else{
    Check(g_state.cleanupDenied==0&&std::distance(fs::directory_iterator(output),fs::directory_iterator{})==1,
        "Ordinary terminal exception cleanup removes only its owned archive and manifest");
    Check(CtBackup::CreateValidated7zBackup(sites,selected,CTSPACES_VERSION_TEXT,nullptr,nullptr).ok(),
        "A fresh backup succeeds after ordinary terminal callback interruption");
  }
  std::cout<<"TERMINAL_SUMMARY operation="<<(verify?"verify":"compress")<<" cleanup_denied="<<denyCleanup
      <<" callback_hit="<<terminal.hit.load()<<" denied_attempts="<<g_state.cleanupDenied<<" result=0x"<<std::hex
      <<static_cast<unsigned long>(result.code)<<std::dec<<"\n";
}

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
  if (kind == FileDispositionInfo && (g_state.fault == Fault::CleanupFailure||g_state.fault==Fault::TerminalCleanupFailure) && IsArchive(file)) {
    ++g_state.cleanupDenied;
    SetLastError(ERROR_ACCESS_DENIED); return FALSE;
  }
  return ::SetFileInformationByHandle(file, kind, info, bytes);
}
} // namespace backup_recovery_boundary

void NoReparseAncestors(const fs::path& path){for(auto current=path;!current.empty();){const auto attributes=GetFileAttributesW(current.c_str());if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Reparse fixture ancestor refused");const auto parent=current.parent_path();if(parent==current)break;current=parent;}}
int wmain(int argc, wchar_t **argv) {
  try {
    std::cout<<std::unitbuf;
    const bool boundMode=argc==4&&(!wcscmp(argv[1],L"--before")||!wcscmp(argv[1],L"--after"))&&!wcscmp(argv[2],L"--fixture");
    Check(argc == 1||boundMode, "unknown arguments refused");
    g_expectBefore=boundMode&&!wcscmp(argv[1],L"--before");
    GUID id{}; Check(SUCCEEDED(CoCreateGuid(&id)), "unique fixture GUID failed");
    const auto root = boundMode?fs::path(argv[3]):fs::current_path() / L"build" /
        std::format(L"br7-{}-{}", GetCurrentProcessId(), GetTickCount64());
    if(boundMode){
      const auto allowed=(fs::current_path()/L"build").lexically_normal();
      const auto normalized=fs::absolute(root).lexically_normal();
      const auto relative=normalized.lexically_relative(allowed);
      if(!root.is_absolute()||normalized!=root||relative.empty()||relative.is_absolute()||*relative.begin()==L".."||root.filename()!=L"f")throw std::runtime_error("Bound fixture must be one fresh f below the workspace build tree");
    }
    NoReparseAncestors(root);
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
    if(g_checks!=95)throw std::runtime_error("The original 95-check/11-scenario fault scope changed unexpectedly");
    std::cout<<"ORIGINAL_CHECKS=95\nORIGINAL_SCENARIOS=11\n";
    for(bool verify:{false,true})for(bool denyCleanup:{false,true})
      TestTerminalProgress(root/(L"t"+std::to_wstring(g_terminalCases)),verify,denyCleanup,source);
    unsigned long long totalBytes=0;
    for(const auto& entry:fs::recursive_directory_iterator(root)){
      NoReparseAncestors(entry.path());if(!entry.is_regular_file())continue;
      const auto bytes=entry.file_size();if(bytes>2ull*1024*1024-totalBytes)throw std::runtime_error("Two MiB retained fixture budget exceeded");totalBytes+=bytes;
    }
    Check(totalBytes<=2ull*1024*1024,"All retained original and terminal fault fixtures remain within two MiB");
    std::cout << "Backup recovery fault checks passed: " << g_checks << " (15 scenarios; seed0x7135A29B).\n";
    std::cout<<"PHYSICAL_FIXTURE_BYTES="<<totalBytes<<"\nTERMINAL_CASES="<<g_terminalCases<<"\nMISSING_RETENTION_PATHS="<<g_expectedDefects<<"\nREGRESSION_PASSED="<<(g_expectedDefects==0)<<"\nUNEXPECTED_FAILURES=0\n";
    std::wcout << L"FIXTURE=" << root << L'\n';
    if(g_expectBefore){std::cout<<"EXPECTED_BEFORE_DEFECTS_REPRODUCED="<<g_expectedDefects<<'\n';return g_expectedDefects==2?0:1;}
    return g_expectedDefects?1:0;
  } catch (const std::exception &error) {
    std::cerr << "Backup recovery check failed: " << error.what() << '\n';
    return 1;
  }
}
