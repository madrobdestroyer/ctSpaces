#include "../BackupEngine.h"
#include "../version.h"
#include "BackupExceptionNativeBoundary.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string_view>
#include <vector>

// The harness arms this one-shot allocation fault at an exact source open or
// at a verified archive's publication boundary.
namespace exception_allocation {
thread_local bool failNext = false;
thread_local unsigned hits = 0;
}
void* operator new(std::size_t bytes) {
  if (exception_allocation::failNext) {
    exception_allocation::failNext = false;
    ++exception_allocation::hits;
    throw std::bad_alloc();
  }
  if (void* value = std::malloc(bytes ? bytes : 1)) return value;
  throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace fs = std::filesystem;
namespace {
unsigned checks=0, expectedDefects=0;
bool legacyBefore=false, currentBefore=false, publicationBefore=false;
struct State {
  bool badAllocation=false, denyCleanup=false, denyCleanupOnce=false, progressAbort=false;
  bool archiveKnown=false, publishing=false;
  bool armAfterRename=false, renamed=false;
  unsigned cleanupDenied=0;
  fs::path archive,destination;
  BY_HANDLE_FILE_INFORMATION identity{};
} state;

struct SourceLeaseObservation;
thread_local SourceLeaseObservation* sourceLease=nullptr;

struct SourceLeaseObservation {
  struct Search {
    fs::path pattern;
    HANDLE handle=INVALID_HANDLE_VALUE;
    bool closed=false;
  };

  fs::path filePath;
  HANDLE file=INVALID_HANDLE_VALUE;
  bool fileClosed=false;
  std::array<Search,2> searches;

  SourceLeaseObservation(const fs::path& sites,const fs::path& source)
      : filePath(source), searches{{{sites/L"*"},{source.parent_path()/L"*"}}} {}
  ~SourceLeaseObservation() {
    sourceLease=nullptr;
    ReleaseUnclosed();
  }

  void ReleaseUnclosed() noexcept {
    // Before proof only: release the exact native handles observed on the
    // disposable fixture after measuring the production failure.
    if (file!=INVALID_HANDLE_VALUE && !fileClosed)
      fileClosed=::CloseHandle(file)!=FALSE;
    for (auto& search:searches)
      if (search.handle!=INVALID_HANDLE_VALUE && !search.closed)
        search.closed=::FindClose(search.handle)!=FALSE;
  }
};

void Check(bool pass,const char* message) {
  ++checks;
  if (!pass) throw std::runtime_error(message);
}
std::vector<unsigned char> Read(const fs::path& path) {
  std::ifstream input(path,std::ios::binary);
  if (!input) throw std::runtime_error("Private fixture read failed");
  return {std::istreambuf_iterator<char>(input),{}};
}
void Write(const fs::path& path,const std::vector<unsigned char>& bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path,std::ios::binary);
  output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
  if (!output) throw std::runtime_error("Private fixture write failed");
}
struct FileState {
  BY_HANDLE_FILE_INFORMATION info{};
  std::vector<unsigned char> bytes;
};
FileState Capture(const fs::path& path) {
  HANDLE file=::CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if (file==INVALID_HANDLE_VALUE) throw std::runtime_error("Private fixture identity open failed");
  BY_HANDLE_FILE_INFORMATION info{};
  const bool good=::GetFileInformationByHandle(file,&info) && ::GetFileType(file)==FILE_TYPE_DISK &&
      !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) && info.nNumberOfLinks==1;
  ::CloseHandle(file);
  if (!good) throw std::runtime_error("Private fixture identity was unsafe");
  return {info,Read(path)};
}
bool Same(const FileState& a,const FileState& b) {
  return a.info.dwVolumeSerialNumber==b.info.dwVolumeSerialNumber &&
      a.info.nFileIndexHigh==b.info.nFileIndexHigh && a.info.nFileIndexLow==b.info.nFileIndexLow &&
      a.info.dwFileAttributes==b.info.dwFileAttributes &&
      ::CompareFileTime(&a.info.ftCreationTime,&b.info.ftCreationTime)==0 &&
      ::CompareFileTime(&a.info.ftLastWriteTime,&b.info.ftLastWriteTime)==0 && a.bytes==b.bytes;
}
void ArchiveBoundary(const fs::path&,const fs::path& archive,
    CtBackup::BackupPublicationBoundary boundary,void*) {
  if (boundary!=CtBackup::BackupPublicationBoundary::ArchiveCreated) return;
  state.archive=archive;
  HANDLE file=::CreateFileW(archive.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
      nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if (file==INVALID_HANDLE_VALUE) throw std::runtime_error("Owned archive observation failed");
  state.archiveKnown=::GetFileInformationByHandle(file,&state.identity)!=FALSE;
  ::CloseHandle(file);
  if (!state.archiveKnown) throw std::runtime_error("Owned archive identity observation failed");
}
void Publishing(void*,const _7zProgressInfo& info) {
  if (info.phase!=_7zProgressPhase::Publishing) return;
  state.publishing=true;
  if (state.progressAbort) throw std::runtime_error("Private ordinary Publishing callback abort");
  if (state.badAllocation) exception_allocation::failNext=true;
}
void RunCase(const fs::path& root,unsigned mode) {
  const auto fixture=root/std::to_wstring(mode),sites=fixture/L"d/Sites",output=fixture/L"o",archive=output/L"previous.7z";
  fs::create_directories(output);
  const std::vector<unsigned char> bytes={'E','X','A','C','T',' ','A','L','P','H','A',' ',0xCE,0xA9};
  Write(sites/L"Alpha/state.bin",bytes);
  state={};
  Check(CtBackup::CreateValidated7zBackup(sites,archive,CTSPACES_VERSION_TEXT,nullptr,nullptr).ok(),"Baseline real SDK backup failed");
  const auto oldArchive=Capture(archive),oldSource=Capture(sites/L"Alpha/state.bin");
  state={};state.badAllocation=(mode==1 || mode==3);state.denyCleanup=(mode==2 || mode==3);
  state.denyCleanupOnce=mode==4;state.progressAbort=(mode==2 || mode==4);
  exception_allocation::failNext=false;exception_allocation::hits=0;
  CtBackup::SetBackupPublicationHookForTest(ArchiveBoundary,nullptr);
  CtBackup::BackupOptions options;options.selectedClientNames=std::vector<std::wstring>{L"Alpha"};options.detailedProgress=Publishing;
  const auto result=CtBackup::CreateValidated7zBackup(sites,archive,CTSPACES_VERSION_TEXT,nullptr,nullptr,options);
  exception_allocation::failNext=false;
  CtBackup::SetBackupPublicationHookForTest(nullptr,nullptr);
  Check(state.publishing && state.archiveKnown,"Real verified archive must reach Publishing before any injected fault");
  Check(exception_allocation::hits==(state.badAllocation?1u:0u),"Only the requested one-shot allocation fault must occur");
  Check(Same(Capture(sites/L"Alpha/state.bin"),oldSource),"Every case preserves exact source content, identity and dates");
  Check(SUCCEEDED(_7zTest7z(archive.c_str(),nullptr,nullptr)),"Prior or successfully replaced destination remains native SDK CRC-valid");
  if (mode==0) {
    Check(result.ok() && !fs::exists(state.archive) && state.cleanupDenied==0,"Success control must publish and leave no staged artifact");
  } else {
    Check(!result.ok(),"Injected failure must not report success");
    Check(Same(Capture(archive),oldArchive),"Injected failure preserves exact old destination bytes, identity and dates");
    Check(result.code==((mode==2 || mode==4)?E_ABORT:E_FAIL),"The precise failure HRESULT must remain visible");
    if (mode==4) {
      Check(state.cleanupDenied==1 && !fs::exists(state.archive),
            "One exact native cleanup refusal must be followed by removal of the temporary archive");
      Check(std::distance(fs::directory_iterator(output),fs::directory_iterator{})==1,
            "Transient cleanup refusal leaves only the exact prior destination");
      const bool hasPath=result.details.find(state.archive.wstring())!=std::wstring::npos;
      std::wcout<<L"TRANSIENT_PATH="<<state.archive.wstring()<<L" EXISTS=0 REPORTED="<<hasPath<<L'\n';
      if (currentBefore) {
        Check(hasPath,"Before proof must report the temporary archive as retained after it was removed");
        ++expectedDefects;
      } else Check(!hasPath,"Cleanup must not report a retained path for a removed archive");
      Check(CtBackup::CreateValidated7zBackup(sites,archive,CTSPACES_VERSION_TEXT,nullptr,nullptr).ok(),
            "A fresh backup succeeds after transient cleanup refusal");
    } else if (state.denyCleanup) {
      Check(state.cleanupDenied>0 && fs::is_regular_file(state.archive),"Exact owned-archive cleanup refusal must retain its file");
      Check(SUCCEEDED(_7zTest7z(state.archive.c_str(),nullptr,nullptr)),"Retained archive remains complete and independently CRC-valid");
      const auto retained=Capture(state.archive);state.denyCleanup=false;
      CtBackup::StagedRestore staged;
      Check(CtBackup::StageValidated7zRestore(state.archive,fixture/L"s",nullptr,nullptr,staged).ok(),"Retained archive can be extracted by the real validated restore backend");
      Check(Read(staged.payloadDir/L"Alpha/state.bin")==bytes && staged.profileCount==1,"Retained archive contains exact selected client data");
      Check(Same(Capture(state.archive),retained),"Validation does not mutate the retained archive");
      const bool hasPath=result.details.find(state.archive.wstring())!=std::wstring::npos;
      if (mode==3 && legacyBefore) {
        Check(!hasPath,"Before proof must reproduce the missing retained path");++expectedDefects;
      } else Check(hasPath,"A cleanup refusal must report the exact retained archive path");
      Check(std::distance(fs::directory_iterator(output),fs::directory_iterator{})==2,"Only old destination and owned retained archive remain, with no manifest leak");
      const auto retainedPath=state.archive;
      state.badAllocation=false;state.progressAbort=false;
      Check(CtBackup::CreateValidated7zBackup(sites,archive,CTSPACES_VERSION_TEXT,nullptr,nullptr).ok(),"A fresh fault-free backup succeeds after the independent failure is removed");
      Check(Same(Capture(retainedPath),retained),"The successful retry preserves the exact retained archive from the failed operation");
      std::wcout<<L"RETAINED_PATH="<<state.archive.wstring()<<L" REPORTED="<<hasPath<<L'\n';
    } else {
      Check(!fs::exists(state.archive) && state.cleanupDenied==0,"Allocation-only failure removes its owned temporary archive");
      Check(std::distance(fs::directory_iterator(output),fs::directory_iterator{})==1,"Allocation-only cleanup leaves the exact prior destination only");
    }
  }
  std::wcout<<L"CASE="<<mode<<L" RESULT=0x"<<std::hex<<static_cast<unsigned long>(result.code)<<std::dec
      <<L" ALLOCATION_HITS="<<exception_allocation::hits<<L" CLEANUP_DENIALS="<<state.cleanupDenied<<L" DETAILS="<<result.details<<L'\n';
}

DWORD OpenSourceForWrite(const fs::path& source) {
  const HANDLE writer=::CreateFileW(source.c_str(),GENERIC_WRITE|DELETE,
      FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if (writer==INVALID_HANDLE_VALUE) return ::GetLastError();
  ::CloseHandle(writer);
  return ERROR_SUCCESS;
}

void RunSourceLeaseCase(const fs::path& root) {
  const auto fixture=root/L"source-lease",sites=fixture/L"d"/L"Sites";
  const auto source=sites/L"Alpha"/L"state.bin",output=fixture/L"o",archive=output/L"previous.7z";
  fs::create_directories(output);
  Write(source,{'P','R','I','V','A','T','E'});
  Check(CtBackup::CreateValidated7zBackup(sites,archive,CTSPACES_VERSION_TEXT,nullptr,nullptr).ok(),
        "Baseline source-lease fixture must create an ordinary native backup");
  const auto oldArchive=Capture(archive),oldSource=Capture(source);
  Check(OpenSourceForWrite(source)==ERROR_SUCCESS,"Source fixture begins without a sharing conflict");

  SourceLeaseObservation observation(sites,source);
  exception_allocation::hits=0;
  sourceLease=&observation;
  CtBackup::BackupOptions options;options.selectedClientNames=std::vector<std::wstring>{L"Alpha"};
  const auto result=CtBackup::CreateValidated7zBackup(sites,archive,CTSPACES_VERSION_TEXT,nullptr,nullptr,options);
  sourceLease=nullptr;
  exception_allocation::failNext=false;
  Check(!result.ok() && result.code==E_FAIL && exception_allocation::hits==1,
        "An allocation failure immediately after exact source open must return E_FAIL once");
  Check(observation.file!=INVALID_HANDLE_VALUE && std::all_of(observation.searches.begin(),observation.searches.end(),
            [](const auto& search){return search.handle!=INVALID_HANDLE_VALUE;}),
        "Both real directory searches and the exact source file were acquired before the fault");
  const auto unclosedSearches=std::count_if(observation.searches.begin(),observation.searches.end(),
      [](const auto& search){return !search.closed;});
  const DWORD writerError=OpenSourceForWrite(source);
  std::cout<<"SOURCE_LEASE FILE_CLOSED="<<observation.fileClosed<<" UNCLOSED_SEARCHES="<<unclosedSearches
           <<" WRITER_ERROR="<<writerError<<'\n';
  if (currentBefore) {
    Check(!observation.fileClosed && writerError==ERROR_SHARING_VIOLATION,
          "Before proof must reproduce the source handle leak and real sharing violation");
    Check(unclosedSearches==2,"Before proof must reproduce both outstanding native search handles");
    ++expectedDefects;
    observation.ReleaseUnclosed();
  } else {
    Check(observation.fileClosed && writerError==ERROR_SUCCESS,
          "An allocation failure must release the source file for an immediate new writer");
    Check(unclosedSearches==0,"An allocation failure must release both native directory searches");
  }
  Check(Same(Capture(source),oldSource) && Same(Capture(archive),oldArchive),
        "The failed backup preserves the exact source and previous archive");
  Check(std::distance(fs::directory_iterator(output),fs::directory_iterator{})==1,
        "The failed backup removes its temporary manifest and leaves the previous archive only");
  Check(CtBackup::CreateValidated7zBackup(sites,archive,CTSPACES_VERSION_TEXT,nullptr,nullptr).ok() &&
        SUCCEEDED(_7zTest7z(archive.c_str(),nullptr,nullptr)),
        "A fault-free retry in the same process makes a complete and independently CRC-valid archive");
}

struct PublicationManifestProbe {
  fs::path path;
  HANDLE reader=INVALID_HANDLE_VALUE;
  FileState original;
  ~PublicationManifestProbe() {if (reader!=INVALID_HANDLE_VALUE) ::CloseHandle(reader);}
};

void HoldManifestThroughPublication(const fs::path& manifest,const fs::path& archive,
    CtBackup::BackupPublicationBoundary boundary,void* user) {
  ArchiveBoundary(manifest,archive,boundary,nullptr);
  if (boundary!=CtBackup::BackupPublicationBoundary::Verified) return;
  auto& probe=*static_cast<PublicationManifestProbe*>(user);
  probe.path=manifest;
  probe.reader=::CreateFileW(manifest.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,
      OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if (probe.reader==INVALID_HANDLE_VALUE) throw std::runtime_error("Exact verified manifest reader unavailable");
  probe.original=Capture(manifest);
}

void RunCommittedResultCase(const fs::path& root) {
  const auto fixture=root/L"committed-result",sites=fixture/L"d"/L"Sites";
  const auto source=sites/L"Alpha"/L"state.bin",output=fixture/L"o";
  const auto archive=output/L"selected.7z",priorCopy=fixture/L"prior-backup.7z";
  fs::create_directories(output);
  Write(source,{'P','R','I','O','R'});
  state={};
  Check(CtBackup::CreateValidated7zBackup(sites,archive,CTSPACES_VERSION_TEXT,nullptr,nullptr).ok(),
        "The destination starts with a real verified backup");
  const auto prior=Capture(archive);
  Check(fs::copy_file(archive,priorCopy) && Read(priorCopy)==prior.bytes,
        "The exact prior backup bytes are retained separately as evidence");
  const std::vector<unsigned char> fresh={'F','R','E','S','H',' ','D','A','T','A'};
  Write(source,fresh);
  const auto sourceBefore=Capture(source);

  PublicationManifestProbe probe;
  state={};state.armAfterRename=true;state.destination=archive.lexically_normal();
  exception_allocation::hits=0;
  CtBackup::SetBackupPublicationHookForTest(HoldManifestThroughPublication,&probe);
  const auto result=CtBackup::CreateValidated7zBackup(sites,archive,CTSPACES_VERSION_TEXT,nullptr,nullptr);
  const bool pending=exception_allocation::failNext;
  exception_allocation::failNext=false;
  CtBackup::SetBackupPublicationHookForTest(nullptr,nullptr);

  Check(state.archiveKnown && state.renamed && !fs::exists(state.archive),
        "The exact native publication replaced the destination using the verified archive identity");
  Check(probe.reader!=INVALID_HANDLE_VALUE && probe.path.parent_path()==output && probe.path!=archive &&
        Same(Capture(probe.path),probe.original),
        "The exact real reader retained only the original temporary manifest bytes and identity");
  Check(std::distance(fs::directory_iterator(output),fs::directory_iterator{})==2,
        "Only the committed backup and its retained temporary manifest remain in the output folder");
  const auto committed=Capture(archive);
  Check(committed.info.dwVolumeSerialNumber==state.identity.dwVolumeSerialNumber &&
        committed.info.nFileIndexHigh==state.identity.nFileIndexHigh && committed.info.nFileIndexLow==state.identity.nFileIndexLow &&
        !Same(prior,committed) && committed.bytes!=prior.bytes && SUCCEEDED(_7zTest7z(archive.c_str(),nullptr,nullptr)),
        "The destination holds the exact new archive identity and independently passes native CRC");
  CtBackup::StagedRestore staged;
  const auto stagedResult=CtBackup::StageValidated7zRestore(archive,fixture/L"s",nullptr,nullptr,staged);
  Check(stagedResult.ok() && staged.profileCount==1 && Read(staged.payloadDir/L"Alpha/state.bin")==fresh,
        "The committed archive actually restores the new selected payload");
  Check(Same(Capture(source),sourceBefore) && Read(priorCopy)==prior.bytes,
        "Publication and verification preserve the exact live source and separately retained prior backup bytes");
  Check(result.details.find(probe.path.wstring())!=std::wstring::npos,
        "The final diagnostic identifies the exact retained temporary manifest");
  std::cout<<"COMMITTED_RESULT published="<<state.renamed<<" result_ok="<<result.ok()
           <<" allocation_hits="<<exception_allocation::hits<<" pending="<<pending<<'\n';
  if (publicationBefore) {
    Check(result.code==E_FAIL && exception_allocation::hits==1 && !pending,
          "Before proof must reproduce false failure after verified replacement already succeeded");
    ++expectedDefects;
  } else {
    Check(result.ok() && exception_allocation::hits==0 && pending,
          "Successful publication reports success without allocating in the retained-manifest reporting path");
  }
  ::CloseHandle(probe.reader);probe.reader=INVALID_HANDLE_VALUE;
  state.armAfterRename=false;
  Check(CtBackup::CreateValidated7zBackup(sites,archive,CTSPACES_VERSION_TEXT,nullptr,nullptr).ok() &&
        Same(Capture(probe.path),probe.original),
        "An ordinary retry succeeds and preserves the independently retained manifest exactly");
}
}
namespace backup_exception_boundary {
BOOL WINAPI SetFileInformationByHandle(HANDLE file,FILE_INFO_BY_HANDLE_CLASS kind,LPVOID info,DWORD size) {
  const auto matchesArchive=[&] {
    BY_HANDLE_FILE_INFORMATION actual{};
    return state.archiveKnown && ::GetFileInformationByHandle(file,&actual) &&
        actual.dwVolumeSerialNumber==state.identity.dwVolumeSerialNumber &&
        actual.nFileIndexHigh==state.identity.nFileIndexHigh && actual.nFileIndexLow==state.identity.nFileIndexLow;
  };
  if (kind==FileDispositionInfo && (state.denyCleanup || state.denyCleanupOnce) && matchesArchive()) {
    state.denyCleanupOnce=false;
    ++state.cleanupDenied;::SetLastError(ERROR_ACCESS_DENIED);return FALSE;
  }
  if (kind==FileRenameInfo && state.armAfterRename && matchesArchive() && info && size>=offsetof(FILE_RENAME_INFO,FileName)) {
    const auto& rename=*static_cast<const FILE_RENAME_INFO*>(info);
    const auto length=rename.FileNameLength;
    if (rename.ReplaceIfExists && length<=size - offsetof(FILE_RENAME_INFO,FileName) && length%sizeof(wchar_t)==0 &&
        std::wstring_view(rename.FileName,length/sizeof(wchar_t))==state.destination.native()) {
      const BOOL renamed=::SetFileInformationByHandle(file,kind,info,size);
      if (renamed) {state.renamed=true;exception_allocation::failNext=true;}
      return renamed;
    }
  }
  return ::SetFileInformationByHandle(file,kind,info,size);
}
}
namespace backup_source_boundary {
HANDLE WINAPI CreateFileW(LPCWSTR path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES attributes,
                          DWORD disposition,DWORD flags,HANDLE templateFile) {
  const HANDLE file=::CreateFileW(path,access,share,attributes,disposition,flags,templateFile);
  if (sourceLease && file!=INVALID_HANDLE_VALUE && sourceLease->file==INVALID_HANDLE_VALUE &&
      access==GENERIC_READ && share==FILE_SHARE_READ && disposition==OPEN_EXISTING &&
      flags==FILE_FLAG_OPEN_REPARSE_POINT && wcscmp(path,sourceLease->filePath.c_str())==0) {
    sourceLease->file=file;
    exception_allocation::failNext=true;
  }
  return file;
}
BOOL WINAPI CloseHandle(HANDLE file) {
  const BOOL closed=::CloseHandle(file);
  if (closed && sourceLease && file==sourceLease->file) sourceLease->fileClosed=true;
  return closed;
}
HANDLE WINAPI FindFirstFileW(LPCWSTR pattern,LPWIN32_FIND_DATAW data) {
  const HANDLE file=::FindFirstFileW(pattern,data);
  if (sourceLease && file!=INVALID_HANDLE_VALUE) {
    for (auto& search:sourceLease->searches)
      if (wcscmp(pattern,search.pattern.c_str())==0) search.handle=file;
  }
  return file;
}
BOOL WINAPI FindClose(HANDLE file) {
  const BOOL closed=::FindClose(file);
  if (closed && sourceLease) {
    for (auto& search:sourceLease->searches)
      if (search.handle==file) search.closed=true;
  }
  return closed;
}
}
HRESULT CtBackupTestCreateGuid(GUID* identity) { return ::CoCreateGuid(identity); }
int wmain(int argc,wchar_t** argv) {
  try {
    if (argc!=4 || wcscmp(argv[2],L"--fixture")) return 2;
    legacyBefore=!wcscmp(argv[1],L"--before");currentBefore=!wcscmp(argv[1],L"--current-before");
    publicationBefore=!wcscmp(argv[1],L"--publication-before");
    const bool after=!wcscmp(argv[1],L"--after");
    if (!legacyBefore && !currentBefore && !publicationBefore && !after) return 2;
    const auto root=fs::absolute(argv[3]);
    if (fs::exists(root)) throw std::runtime_error("A fresh, owned fixture is required");
    Check(fs::create_directory(root),"Fresh fixture root could not be created");
    if (legacyBefore || after)
      for (unsigned mode=0;mode<4;++mode) RunCase(root,mode);
    if (currentBefore || after) {
      RunCase(root,4);
      RunSourceLeaseCase(root);
    }
    if (publicationBefore || after) RunCommittedResultCase(root);
    Check(expectedDefects==(after?0u:(currentBefore?2u:1u)),"Expected defect count differs");
    const unsigned scenarios=legacyBefore?4u:(currentBefore?2u:(publicationBefore?1u:7u));
    std::cout<<"SCENARIOS="<<scenarios<<"\nEXPECTED_DEFECTS="<<expectedDefects<<"\nREGRESSION_PASSED="<<after
        <<"\nNO_UI_OR_DEVICE_INPUT=1\nBackup exception recovery checks passed: "<<checks<<".\n";
    return 0;
  } catch(const std::exception& error) {
    exception_allocation::failNext=false;CtBackup::SetBackupPublicationHookForTest(nullptr,nullptr);
    std::cerr<<"Backup exception recovery failure: "<<error.what()<<'\n';return 1;
  }
}
