// Bounded ordinary-file hashing regression/benchmark. This translation unit
// includes the real backend; native interception exists only in this test.
#include "../BackupEngine.h"
#include <bcrypt.h>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace HashBoundary {
enum class Fail { None, Open, Property, Create, Read, Data, Finish };
struct Counters {
  unsigned opens=0, closes=0, properties=0, creates=0, destroys=0;
  unsigned liveAlgorithms=0, liveHashes=0, reads=0;
  Fail fail=Fail::None;
};
thread_local Counters state;
thread_local size_t failAllocationBytes=0;
bool Consume(Fail failure) {
  if (state.fail!=failure) return false;
  state.fail=Fail::None;
  return true;
}
constexpr NTSTATUS failed=static_cast<NTSTATUS>(0xC0000001UL);
NTSTATUS WINAPI Open(BCRYPT_ALG_HANDLE* algorithm,LPCWSTR id,LPCWSTR implementation,ULONG flags) {
  ++state.opens;
  if(Consume(Fail::Open)) return failed;
  const auto result=::BCryptOpenAlgorithmProvider(algorithm,id,implementation,flags);
  if(result>=0) ++state.liveAlgorithms;
  return result;
}
NTSTATUS WINAPI Close(BCRYPT_ALG_HANDLE algorithm,ULONG flags) {
  ++state.closes;
  const auto result=::BCryptCloseAlgorithmProvider(algorithm,flags);
  if(result>=0) --state.liveAlgorithms;
  return result;
}
NTSTATUS WINAPI Property(BCRYPT_HANDLE object,LPCWSTR property,PUCHAR output,ULONG bytes,ULONG* returned,ULONG flags) {
  ++state.properties;
  if(Consume(Fail::Property)) return failed;
  return ::BCryptGetProperty(object,property,output,bytes,returned,flags);
}
NTSTATUS WINAPI Create(BCRYPT_ALG_HANDLE algorithm,BCRYPT_HASH_HANDLE* hash,PUCHAR object,ULONG bytes,PUCHAR secret,ULONG secretBytes,ULONG flags) {
  ++state.creates;
  if(Consume(Fail::Create)) return failed;
  const auto result=::BCryptCreateHash(algorithm,hash,object,bytes,secret,secretBytes,flags);
  if(result>=0) ++state.liveHashes;
  return result;
}
NTSTATUS WINAPI Destroy(BCRYPT_HASH_HANDLE hash) {
  ++state.destroys;
  const auto result=::BCryptDestroyHash(hash);
  if(result>=0) --state.liveHashes;
  return result;
}
NTSTATUS WINAPI Data(BCRYPT_HASH_HANDLE hash,PUCHAR input,ULONG bytes,ULONG flags) {
  if(Consume(Fail::Data)) return failed;
  return ::BCryptHashData(hash,input,bytes,flags);
}
NTSTATUS WINAPI Finish(BCRYPT_HASH_HANDLE hash,PUCHAR output,ULONG bytes,ULONG flags) {
  if(Consume(Fail::Finish)) return failed;
  return ::BCryptFinishHash(hash,output,bytes,flags);
}
BOOL WINAPI Read(HANDLE file,LPVOID buffer,DWORD bytes,LPDWORD read,LPOVERLAPPED overlapped) {
  ++state.reads;
  if(Consume(Fail::Read)) {SetLastError(ERROR_READ_FAULT);return FALSE;}
  return ::ReadFile(file,buffer,bytes,read,overlapped);
}
}
// This narrow one-shot allocator fault affects only this isolated harness.
void* operator new(size_t bytes) {
  // MSVC adds a small alignment header for allocations larger than one page.
  if(HashBoundary::failAllocationBytes && bytes>=HashBoundary::failAllocationBytes &&
     bytes-HashBoundary::failAllocationBytes<=64) {HashBoundary::failAllocationBytes=0;throw std::bad_alloc();}
  if(void* memory=std::malloc(bytes?bytes:1))return memory;
  throw std::bad_alloc();
}
void operator delete(void* memory) noexcept {std::free(memory);}
void operator delete(void* memory,size_t) noexcept {std::free(memory);}
#define BCryptOpenAlgorithmProvider HashBoundary::Open
#define BCryptCloseAlgorithmProvider HashBoundary::Close
#define BCryptGetProperty HashBoundary::Property
#define BCryptCreateHash HashBoundary::Create
#define BCryptDestroyHash HashBoundary::Destroy
#define BCryptHashData HashBoundary::Data
#define BCryptFinishHash HashBoundary::Finish
#define ReadFile HashBoundary::Read
#include "../BackupEngine.cpp"
namespace CtBackup { namespace {
// Frozen literal October-29 implementation, before this optimization.
// Original bytes SHA256 937C63A5AA8C2CE28F9BEB9472ABBB20B646A4C9195BDB9306E8E8B336B3E0D6.
Result BaselineHashRestoreFile(HANDLE file, std::array<unsigned char, 32> &digest) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  struct CloseHash {
    BCRYPT_ALG_HANDLE &algorithm; BCRYPT_HASH_HANDLE &hash;
    ~CloseHash() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
  } cleanup{algorithm, hash};
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
    return Failure(E_FAIL, L"Restore content verification could not initialize SHA-256.");
  DWORD objectLength = 0, returned = 0;
  if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength),
                        sizeof(objectLength), &returned, 0) < 0)
    return Failure(E_FAIL, L"Restore content verification could not prepare SHA-256.");
  std::vector<unsigned char> object(objectLength);
  if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) < 0)
    return Failure(E_FAIL, L"Restore content verification could not create SHA-256.");
  std::array<unsigned char, 64 * 1024> buffer{};
  for (;;) {
    DWORD read = 0;
    if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
      return Failure(HRESULT_FROM_WIN32(GetLastError()), L"A staged restore file changed or could not be read.");
    if (read == 0) break;
    if (BCryptHashData(hash, buffer.data(), read, 0) < 0)
      return Failure(E_FAIL, L"Restore content verification failed.");
  }
  if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
    return Failure(E_FAIL, L"Restore content verification could not finish SHA-256.");
  return {};
}


} }
#undef BCryptOpenAlgorithmProvider
#undef BCryptCloseAlgorithmProvider
#undef BCryptGetProperty
#undef BCryptCreateHash
#undef BCryptDestroyHash
#undef BCryptHashData
#undef BCryptFinishHash
#undef ReadFile
#ifndef CTSPACES_HASH_FIXTURE_PARENT
#error Use Test-RestoreHashEfficiency.ps1 to bind a fresh private fixture directory.
#endif
namespace {
using Digest=std::array<unsigned char,32>;
unsigned checks=0;
void Check(bool good,const char* message) {
  ++checks;std::cout<<(good?"PASS ":"FAIL ")<<message<<std::endl;
  if(!good)throw std::runtime_error(message);
}
std::string Hex(const Digest& digest) {
  std::ostringstream text;
  for(auto byte:digest)text<<std::hex<<std::setw(2)<<std::setfill('0')<<static_cast<unsigned>(byte);
  return text.str();
}
struct File {fs::path path;Digest expected{};unsigned long long bytes=0;};
std::vector<File> files;
unsigned long long fixtureBytes=0;
void NoReparse(const fs::path& path) {
  for(auto item=path;!item.empty();) {
    const DWORD attributes=GetFileAttributesW(item.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Reparse fixture ancestor refused");
    const auto parent=item.parent_path();if(parent==item)break;item=parent;
  }
}
fs::path Reserve(const wchar_t* text) {
  const fs::path requested(text),parent(CTSPACES_HASH_FIXTURE_PARENT);
  if(!requested.is_absolute() || requested.lexically_normal()!=requested || requested.parent_path()!=parent || requested.filename()!=L"f")throw std::runtime_error("Compile-bound fresh fixture required");
  NoReparse(requested);
  if(!CreateDirectoryW(requested.c_str(),nullptr))throw std::runtime_error("Existing or unavailable fixture refused");
  NoReparse(requested);return requested;
}
void Write(const fs::path& path,const std::vector<unsigned char>& data) {
  fixtureBytes+=data.size();if(fixtureBytes>32ull*1024*1024)throw std::runtime_error("32MiB fixture bound exceeded");
  NoReparse(path);
  HANDLE output=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(output==INVALID_HANDLE_VALUE)throw std::runtime_error("Fresh fixture output refused");
  DWORD wrote=0;const bool good=WriteFile(output,data.data(),static_cast<DWORD>(data.size()),&wrote,nullptr) && wrote==data.size();
  CloseHandle(output);if(!good)throw std::runtime_error("Fixture write failed");
  Digest expected{};
  if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(data.data()),static_cast<ULONG>(data.size()),expected.data(),static_cast<ULONG>(expected.size()))<0)throw std::runtime_error("Independent one-shot SHA reference failed");
  files.push_back({path,expected,data.size()});
}
struct FileHandle {
  HANDLE value=INVALID_HANDLE_VALUE;
  explicit FileHandle(const fs::path& path) {
    value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(value==INVALID_HANDLE_VALUE)throw std::runtime_error("Private hash input unavailable");
  }
  ~FileHandle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
void Rewind(HANDLE file) {LARGE_INTEGER zero{};if(!SetFilePointerEx(file,zero,nullptr,FILE_BEGIN))throw std::runtime_error("Private hash seek failed");}
struct Timing {double seconds=0;HashBoundary::Counters counters;};
unsigned ExpectedReads(bool baseline) {
  const unsigned long long block=baseline?64ull*1024:256ull*1024;
  unsigned total=0;
  for(const auto& file:files)total+=static_cast<unsigned>((file.bytes+block-1)/block+1);
  return total;
}
Timing Run(bool baseline) {
  HashBoundary::state={};
  const auto start=std::chrono::steady_clock::now();
  {
    CtBackup::RestoreHashContext context;
    for(const auto& file:files) {
      FileHandle input(file.path);Digest actual{};
      const auto result=baseline?CtBackup::BaselineHashRestoreFile(input.value,actual):context.HashFile(input.value,actual);
      if(!result.ok() || actual!=file.expected)throw std::runtime_error("Baseline/current digest disagrees with independent reference");
    }
  }
  return {std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(),HashBoundary::state};
}
Timing RunLarge(bool baseline,const File& file) {
  HashBoundary::state={};
  const auto start=std::chrono::steady_clock::now();
  {
    CtBackup::RestoreHashContext context;FileHandle input(file.path);
    for(unsigned repeat=0;repeat<8;++repeat) {
      Rewind(input.value);Digest actual{};
      const auto result=baseline?CtBackup::BaselineHashRestoreFile(input.value,actual):context.HashFile(input.value,actual);
      if(!result.ok() || actual!=file.expected)throw std::runtime_error("Focused large-file digest differs from independent reference");
    }
  }
  return {std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(),HashBoundary::state};
}
void Failures(const File& file) {
  for(auto fault:{HashBoundary::Fail::Open,HashBoundary::Fail::Property,HashBoundary::Fail::Create,HashBoundary::Fail::Read,HashBoundary::Fail::Data,HashBoundary::Fail::Finish}) {
    HashBoundary::state={};
    {
      CtBackup::RestoreHashContext context;FileHandle input(file.path);Digest actual{};
      HashBoundary::state.fail=fault;
      const auto rejected=context.HashFile(input.value,actual);
      Check(!rejected.ok(),"Native hash/provider/read failure is reported without a successful digest");
      Check(rejected.code==(fault==HashBoundary::Fail::Read?HRESULT_FROM_WIN32(ERROR_READ_FAULT):E_FAIL),"Hash error preserves the expected failure code");
      Rewind(input.value);
      const auto retry=context.HashFile(input.value,actual);
      Check(retry.ok() && actual==file.expected,"Same context retries with a fresh independent file hash after a native failure");
      Check(HashBoundary::state.liveHashes==0,"Per-file hash object closes after failure and successful retry");
    }
    Check(HashBoundary::state.liveAlgorithms==0 && HashBoundary::state.liveHashes==0,"Context destruction closes every owned provider/hash on failure paths");
  }
  HashBoundary::state={};
  {
    CtBackup::RestoreHashContext context;FileHandle input(file.path);Digest actual{};
    bool allocationRejected=false;
    HashBoundary::failAllocationBytes=256*1024;
    try {context.HashFile(input.value,actual);} catch(const std::bad_alloc&) {allocationRejected=true;}
    Check(allocationRejected && !HashBoundary::failAllocationBytes,"Read-workspace allocation failure is explicitly exercised");
    Check(!HashBoundary::state.liveAlgorithms && !HashBoundary::state.liveHashes,"Throwing workspace allocation closes its pending provider before propagating");
    Rewind(input.value);
    Check(context.HashFile(input.value,actual).ok() && actual==file.expected,"Context retries with a fully prepared buffer after allocation failure");
  }
  Check(!HashBoundary::state.liveAlgorithms && !HashBoundary::state.liveHashes,"Allocation-failure retry leaves no owned native hash/provider handles");
}
void Capture(const fs::path& root) {
  HashBoundary::state={};std::vector<CtBackup::RestoreValidationEntry> first;
  Check(CtBackup::CaptureRestoreTree(root,first).ok(),"Actual recursive restore-tree capture accepts the bounded ordinary fixture");
  Check(HashBoundary::state.opens==1 && HashBoundary::state.closes==1 && HashBoundary::state.properties==1,"Production tree capture prepares one provider/workspace across all recursive files");
  Check(HashBoundary::state.creates==files.size() && HashBoundary::state.destroys==files.size(),"Production tree capture creates and destroys a fresh per-file hash state");
  bool exact=true;for(const auto& file:files) {
    const auto relative=file.path.lexically_relative(root);
    const auto found=std::find_if(first.begin(),first.end(),[&](const auto& entry){return entry.relative==relative;});
    exact=exact && found!=first.end() && found->digest==file.expected;
  }
  Check(exact,"Every production tree digest agrees with an independent one-shot SHA reference");
  const auto changed=files.front().path;
  HANDLE output=CreateFileW(changed.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(output==INVALID_HANDLE_VALUE)throw std::runtime_error("Owned corruption fixture unavailable");
  DWORD wrote=0;const char altered='z';const bool good=WriteFile(output,&altered,1,&wrote,nullptr) && wrote==1;CloseHandle(output);
  if(!good)throw std::runtime_error("Owned corruption write failed");
  std::vector<CtBackup::RestoreValidationEntry> after;
  Check(CtBackup::CaptureRestoreTree(root,after).ok() && !CtBackup::SameRestoreTree(first,after),"Repeated production capture still detects same-size payload corruption");
}
}
int wmain(int argc,wchar_t** argv) {
  try {
    if(argc!=3 || wcscmp(argv[1],L"--headless"))throw std::runtime_error("Explicit headless fixture required");
    const auto root=Reserve(argv[2]);
    static_assert(sizeof(CtBackup::RestoreHashContext)<1024,"Scan workspace must remain off the worker's small stack");
    fs::create_directory(root/L"small");fs::create_directory(root/L"large");
    Write(root/L"small/abc.bin",{'a','b','c'});Write(root/L"small/empty.bin",{});
    Check(Hex(files[0].expected)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","Independent abc digest matches standard SHA-256 known answer");
    Check(Hex(files[1].expected)=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","Independent empty digest matches standard SHA-256 known answer");
    unsigned random=0x635030u;
    for(unsigned index=0;index<2048;++index) {
      std::vector<unsigned char> data(128+index%896);
      for(auto& byte:data){random^=random<<13;random^=random>>17;random^=random<<5;byte=static_cast<unsigned char>(random>>8);}
      Write(root/L"small"/(std::to_wstring(index)+L".bin"),data);
    }
    std::vector<unsigned char> large(16u*1024u*1024u);
    for(auto& byte:large){random^=random<<13;random^=random>>17;random^=random<<5;byte=static_cast<unsigned char>(random>>8);}
    Write(root/L"large/payload.bin",large);large.clear();large.shrink_to_fit();
    Check(fixtureBytes<=32ull*1024*1024,"All synthetic payloads remain below the explicit 32MiB budget");
    std::vector<double> baselineSeconds,currentSeconds;
    for(unsigned trial=0;trial<3;++trial) {
      Timing before,after;
      if(trial%2==0){before=Run(true);after=Run(false);}else{after=Run(false);before=Run(true);}
      Check(before.counters.opens==files.size() && before.counters.closes==files.size(),"Frozen baseline opens/closes one provider per file");
      Check(after.counters.opens==1 && after.counters.closes==1 && after.counters.properties==1,"Optimized scan opens/closes one provider and prepares workspace once");
      Check(after.counters.creates==files.size() && after.counters.destroys==files.size() && !after.counters.liveHashes && !after.counters.liveAlgorithms,"Optimized scan preserves fresh hashes without native handle leaks");
      Check(before.counters.reads==ExpectedReads(true),"Frozen baseline uses the expected 64KiB reads including each EOF check");
      Check(after.counters.reads==ExpectedReads(false) && after.counters.reads<before.counters.reads,"Optimized 256KiB reads reduce native calls while hashing every byte and EOF");
      baselineSeconds.push_back(before.seconds);currentSeconds.push_back(after.seconds);
      std::cout<<"TIMING trial="<<trial<<" files="<<files.size()<<" payload_bytes="<<fixtureBytes<<" before_seconds="<<before.seconds<<" after_seconds="<<after.seconds<<" before_providers="<<before.counters.opens<<" after_providers="<<after.counters.opens<<" before_reads="<<before.counters.reads<<" after_reads="<<after.counters.reads<<'\n';
    }
    std::vector<double> largeBefore,largeAfter;
    for(unsigned trial=0;trial<5;++trial) {
      Timing before,after;
      if(trial%2==0){before=RunLarge(true,files.back());after=RunLarge(false,files.back());}else{after=RunLarge(false,files.back());before=RunLarge(true,files.back());}
      Check(before.counters.reads==8*(256+1) && after.counters.reads==8*(64+1),"Focused 16MiB-file hashes read the same bytes with fourfold fewer data-read calls");
      Check(before.counters.opens==8 && after.counters.opens==1 && !after.counters.liveAlgorithms && !after.counters.liveHashes,"Focused repeated hashes reuse one context without native handle leaks");
      largeBefore.push_back(before.seconds);largeAfter.push_back(after.seconds);
      std::cout<<"LARGE_TIMING trial="<<trial<<" repeats=8 bytes_per_file="<<files.back().bytes<<" before_seconds="<<before.seconds<<" after_seconds="<<after.seconds<<" before_reads="<<before.counters.reads<<" after_reads="<<after.counters.reads<<'\n';
    }
    Failures(files.back());Capture(root);
    std::sort(baselineSeconds.begin(),baselineSeconds.end());std::sort(currentSeconds.begin(),currentSeconds.end());
    std::sort(largeBefore.begin(),largeBefore.end());std::sort(largeAfter.begin(),largeAfter.end());
    std::cout<<"LARGE_MEDIAN_BEFORE_SECONDS="<<largeBefore[2]<<"\nLARGE_MEDIAN_AFTER_SECONDS="<<largeAfter[2]<<"\nLARGE_TRIALS=5\nLARGE_REPEATS_PER_TRIAL=8\nBASELINE_BUFFER_BYTES=65536\nCURRENT_BUFFER_BYTES=262144\nBASELINE_TREE_READS="<<ExpectedReads(true)<<"\nCURRENT_TREE_READS="<<ExpectedReads(false)<<'\n';
    std::cout<<"MEDIAN_BEFORE_SECONDS="<<baselineSeconds[1]<<"\nMEDIAN_AFTER_SECONDS="<<currentSeconds[1]<<"\nFILES="<<files.size()<<"\nPAYLOAD_BYTES="<<fixtureBytes<<"\nTRIALS=3\nCHECKS="<<checks<<"\nREGRESSION_PASSED=1\n";
    return 0;
  } catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<"\nREGRESSION_PASSED=0\n";return 1;}
}
