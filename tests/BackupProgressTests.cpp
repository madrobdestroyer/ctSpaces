#include "../BackupEngine.h"
#include "../InProc7z.cpp"
#include "../version.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace {
unsigned checks = 0, failures = 0;
void Check(bool value, const char *message) {
  ++checks;
  if (!value) ++failures;
  std::cout << (value ? "PASS " : "FAIL ") << message << '\n';
}
struct Event { _7zOp operation; unsigned percent; DWORD thread; };
struct Events {
  std::mutex mutex;
  std::vector<Event> values;
  DWORD owner = GetCurrentThreadId();
  struct Detail { _7zProgressPhase phase; unsigned long long completed,total,items; bool known; DWORD thread; double seconds; };
  std::vector<Detail> details;
  std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
};
void Record(void *user, _7zOp operation, unsigned percent, const wchar_t *) {
  auto &events = *static_cast<Events *>(user);
  std::lock_guard<std::mutex> lock(events.mutex);
  events.values.push_back({operation, percent, GetCurrentThreadId()});
}
void RecordDetail(void *user,const _7zProgressInfo &info) {
  auto &events=*static_cast<Events *>(user);
  std::lock_guard<std::mutex> lock(events.mutex);
  events.details.push_back({info.phase,info.completedBytes,info.totalBytes,info.processedItems,info.totalKnown,
      GetCurrentThreadId(),std::chrono::duration<double>(std::chrono::steady_clock::now()-events.started).count()});
}
void ThrowHalf(void *,_7zOp,unsigned percent,const wchar_t *) {
  if(percent==50)throw std::runtime_error("Scoped worker callback failure");
}
bool HasWorkerHalf(const Events &events) {
  return std::any_of(events.values.begin(), events.values.end(), [&](const Event &event) {
    return event.percent == 50 && event.thread != events.owner;
  });
}
void BridgeCases() {
  {
    Events events;
    _7zScopedProgress progress(_7zOp::Compress, Record, &events);
    auto *native = new CArchiveUpdateCallback;
    CMyComPtr<IArchiveUpdateCallback2> callback(native);
    native->Init(nullptr);
    callback->SetTotal(100);
    std::thread worker([&]() { const UInt64 completed = 50; callback->SetCompleted(&completed); });
    worker.join();
    std::cout << "BRIDGE operation=compress owner=" << events.owner << " reports=" << events.values.size() << " worker_half=" << HasWorkerHalf(events) << '\n';
    Check(HasWorkerHalf(events), "Compression progress created on one thread survives SDK-worker reporting");
  }
  {
    Events events;
    _7zScopedProgress progress(_7zOp::Extract, Record, &events);
    auto *native = new CArchiveExtractCallback;
    CMyComPtr<IArchiveExtractCallback> callback(native);
    native->Init(nullptr, FString());
    callback->SetTotal(100);
    std::thread worker([&]() { const UInt64 completed = 50; callback->SetCompleted(&completed); });
    worker.join();
    std::cout << "BRIDGE operation=verify owner=" << events.owner << " reports=" << events.values.size() << " worker_half=" << HasWorkerHalf(events) << '\n';
    Check(HasWorkerHalf(events), "Verification progress created on one thread survives SDK-worker reporting");
  }
  for(bool extract:{false,true}) {
    _7zScopedProgress progress(extract?_7zOp::Extract:_7zOp::Compress,ThrowHalf,nullptr);
    HRESULT rejected=S_OK,later=S_OK;
    if(extract) {
      auto *native=new CArchiveExtractCallback;CMyComPtr<IArchiveExtractCallback> callback(native);
      native->Init(nullptr,FString());callback->SetTotal(100);
      std::thread worker([&](){const UInt64 half=50,more=80;rejected=callback->SetCompleted(&half);later=callback->SetCompleted(&more);});worker.join();
      Check(rejected==E_ABORT && later==E_ABORT && native->ProgressAborted(),"A verification worker callback exception is a sticky COM abort");
    } else {
      auto *native=new CArchiveUpdateCallback;CMyComPtr<IArchiveUpdateCallback2> callback(native);
      native->Init(nullptr);callback->SetTotal(100);
      std::thread worker([&](){const UInt64 half=50,more=80;rejected=callback->SetCompleted(&half);later=callback->SetCompleted(&more);});worker.join();
      Check(rejected==E_ABORT && later==E_ABORT && native->ProgressAborted(),"A compression worker callback exception is a sticky COM abort");
    }
  }
  {
    Events first,second;
    _7zScopedProgress outer(_7zOp::Compress,Record,&first);
    auto *firstNative=new CArchiveUpdateCallback;CMyComPtr<IArchiveUpdateCallback2> firstCallback(firstNative);firstNative->Init(nullptr);
    {
      _7zScopedProgress inner(_7zOp::Extract,Record,&second);
      auto *secondNative=new CArchiveExtractCallback;CMyComPtr<IArchiveExtractCallback> secondCallback(secondNative);secondNative->Init(nullptr,FString());
      firstCallback->SetTotal(100);secondCallback->SetTotal(100);
      std::thread firstWorker([&](){const UInt64 value=50;firstCallback->SetCompleted(&value);});
      std::thread secondWorker([&](){const UInt64 value=50;secondCallback->SetCompleted(&value);});
      firstWorker.join();secondWorker.join();
    }
    Check(HasWorkerHalf(first) && HasWorkerHalf(second) &&
      std::all_of(first.values.begin(),first.values.end(),[](const Event &e){return e.operation==_7zOp::Compress;}) &&
      std::all_of(second.values.begin(),second.values.end(),[](const Event &e){return e.operation==_7zOp::Extract;}),
      "Concurrent callback objects retain their independent operation and recipient");
  }
  {
    Events events;
    _7zScopedProgress scope(_7zOp::Compress,nullptr,nullptr,RecordDetail,&events);
    auto *native=new CArchiveUpdateCallback;CMyComPtr<IArchiveUpdateCallback2> callback(native);native->Init(nullptr);
    callback->SetTotal(0);
    Check(events.details.size()==1 && !events.details.back().known,"A zero-byte total is explicitly unknown in detailed progress");
    callback->SetTotal(100);
    std::thread worker([&](){const UInt64 value=50;callback->SetCompleted(&value);});worker.join();
    Check(std::any_of(events.details.begin(),events.details.end(),[&](const Events::Detail &e){return e.known && e.completed==50 && e.total==100 && e.thread!=events.owner;}),
      "Detailed byte progress survives codec-worker dispatch without a legacy callback");
  }
}
std::vector<char> ReadBytes(const fs::path &path) {
  std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("Private progress fixture read failed");
  return {std::istreambuf_iterator<char>(input),{}};
}
void WriteBytes(const fs::path &path,const std::string &bytes) {
  fs::create_directories(path.parent_path());std::ofstream output(path,std::ios::binary);output.write(bytes.data(),bytes.size());
  if(!output)throw std::runtime_error("Private progress fixture write failed");
}
bool TextEquals(const fs::path &path,const std::string &expected) {
  const auto bytes=ReadBytes(path);return std::string(bytes.begin(),bytes.end())==expected;
}
bool OnlyClient(const fs::path &archive,const std::wstring &name) {
  _7zArchiveInfo info;
  return SUCCEEDED(_7zInspect7z(archive.c_str(),&info)) && info.sitesProfileCount==1 && info.sitesClientDirectoryNames.size()==1 &&
      client_name_comparison::EqualIgnoringCase(info.sitesClientDirectoryNames[0],name);
}
struct SelectionMutation { CtBackup::BackupOptions *options;fs::path sites;bool prepared=false,scanned=false; };
struct ThrowDetail { _7zProgressPhase phase;std::atomic<bool> hit{false}; };
void ThrowMeasured(void *user,const _7zProgressInfo &info) {
  auto &state=*static_cast<ThrowDetail *>(user);
  if(info.phase==state.phase && info.totalKnown && info.completedBytes){state.hit.store(true);throw std::runtime_error("Scoped measured progress failure");}
}
void MutateExcluded(void *user,const _7zProgressInfo &info) {
  auto &state=*static_cast<SelectionMutation *>(user);
  if(info.phase==_7zProgressPhase::Preparing && !state.prepared) {
    state.prepared=true;state.options->selectedClientNames=std::vector<std::wstring>{L"Beta"};
    WriteBytes(state.sites/L"New unselected/state.bin","new client added after the selection snapshot");
  }
  if(info.phase==_7zProgressPhase::Scanning && !state.scanned) {
    state.scanned=true;WriteBytes(state.sites/L"Beta/changing.bin","excluded browser state changed during backup");
  }
}
void SubsetCases(const fs::path &root) {
  if(fs::exists(root))throw std::runtime_error("Subset fixture must be new");
  const auto sites=root/L"data/Sites",selected=root/L"selected.7z";
  WriteBytes(sites/L"Alpha/state.bin","selected client exact bytes");
  WriteBytes(sites/L"Alpha/client-notes.json","selected client notes");
  WriteBytes(sites/L"Beta/locked.bin","excluded client exact bytes");
  WriteBytes(sites/L"Beta/changing.bin","excluded browser state before backup");
  {
    std::ofstream large(sites/L"Beta/large-excluded.bin",std::ios::binary);
    large.seekp(64ll*1024ll*1024ll-1);large.put('\0');
    if(!large)throw std::runtime_error("Excluded large fixture could not be prepared");
  }
  WriteBytes(sites/L"unexpected-root-file.txt","not a client and excluded from explicit selection");
  HANDLE lock=CreateFileW((sites/L"Beta/locked.bin").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("Excluded private file lock failed");
  struct Close {HANDLE value;~Close(){CloseHandle(value);}} close{lock};
  CtBackup::BackupOptions options;options.selectedClientNames=std::vector<std::wstring>{L"Alpha"};
  Check(CtBackup::CreateValidated7zBackup(sites,selected,CTSPACES_VERSION_TEXT,nullptr,nullptr,options).ok(),
      "A selected backup succeeds without opening a locked excluded client or unexpected root file");
  Check(OnlyClient(selected,L"Alpha"),"Selected archive contains exactly its selected client and profile count");
  CtBackup::StagedRestore restored;
  Check(CtBackup::StageValidated7zRestore(selected,root/L"restore",nullptr,nullptr,restored).ok() && restored.profileCount==1 &&
      ReadBytes(restored.payloadDir/L"Alpha/state.bin")==ReadBytes(sites/L"Alpha/state.bin") &&
      !fs::exists(restored.payloadDir/L"Beta"),"Selected backup retains the existing restore format and exact included data");
  const auto previous=ReadBytes(selected);
  for(const auto &names:std::vector<std::vector<std::wstring>>{{},{L"Alpha",L"alpha"},{L"../Beta"},{L"Missing"},{L"Alpha",L"Missing"},{L"Beta"}}) {
    options.selectedClientNames=names;
    const auto result=CtBackup::CreateValidated7zBackup(sites,selected,CTSPACES_VERSION_TEXT,nullptr,nullptr,options);
    Check(!result.ok() && ReadBytes(selected)==previous,"Invalid, empty, duplicate, missing or unavailable selections preserve the previous backup exactly");
  }
  Check(!CtBackup::CreateValidated7zBackup(sites,selected,CTSPACES_VERSION_TEXT,nullptr,nullptr).ok() && ReadBytes(selected)==previous,
      "Default all-client backups keep their strict unexpected-root-item refusal");
  options.selectedClientNames=std::vector<std::wstring>{L"Alpha"};
  SelectionMutation mutation{&options,sites};options.detailedProgress=MutateExcluded;options.detailedProgressUser=&mutation;
  Check(CtBackup::CreateValidated7zBackup(sites,selected,CTSPACES_VERSION_TEXT,nullptr,nullptr,options).ok() && mutation.prepared && mutation.scanned && OnlyClient(selected,L"Alpha"),
      "Captured selection ignores later option changes, new excluded clients and excluded browser writes");
  Check(TextEquals(sites/L"Beta/changing.bin","excluded browser state changed during backup"),
      "The backup does not lock or alter excluded browser data");
  options.selectedClientNames=std::vector<std::wstring>{L"Alpha"};options.detailedProgress=nullptr;options.detailedProgressUser=nullptr;
  Check(TextEquals(sites/L"Alpha/state.bin","selected client exact bytes"),
      "Selected source bytes remain unchanged through subset failure and retry cases");
  bool created=false;
  const auto manifest=root/L"manifest.txt";WriteBytes(manifest,"private metadata");
  const std::vector<std::wstring> empty,unsafe{L"../Beta"};
  Check(FAILED(_7zCompress7zWithExtraFile((root/L"empty.7z").c_str(),sites.c_str(),true,manifest.c_str(),L"manifest",nullptr,nullptr,&created,nullptr,nullptr,nullptr,nullptr,&empty)) && !created,
      "Low-level explicit empty selection cannot fall back to all clients");
  Check(FAILED(_7zCompress7zWithExtraFile((root/L"unsafe.7z").c_str(),sites.c_str(),true,manifest.c_str(),L"manifest",nullptr,nullptr,&created,nullptr,nullptr,nullptr,nullptr,&unsafe)) && !created,
      "Low-level unsafe selection rejects before creating an archive");
  for(const auto phase:{_7zProgressPhase::Compressing,_7zProgressPhase::Verifying}) {
    const auto before=ReadBytes(selected);ThrowDetail fault{phase};
    options.detailedProgress=ThrowMeasured;options.detailedProgressUser=&fault;
    const auto result=CtBackup::CreateValidated7zBackup(sites,selected,CTSPACES_VERSION_TEXT,nullptr,nullptr,options);
    Check(fault.hit.load() && !result.ok() && ReadBytes(selected)==before,
        "A measured compression or verification callback failure preserves the exact prior backup");
  }
  options.detailedProgress=nullptr;options.detailedProgressUser=nullptr;
  Check(CtBackup::CreateValidated7zBackup(sites,selected,CTSPACES_VERSION_TEXT,nullptr,nullptr,options).ok(),
      "A fresh backup succeeds after a callback-aborted operation");
  Events parallelEvents[2];CtBackup::Result parallelResults[2];
  std::jthread workers[2];
  for(unsigned index=0;index<2;++index) {
    const auto operation=root/(L"parallel-"+std::to_wstring(index));
    const auto name=index?L"Parallel Beta":L"Parallel Alpha";
    WriteBytes(operation/L"data/Sites"/name/L"state.bin",index?"independent second client":"independent first client");
    fs::create_directories(operation/L"output");
    workers[index]=std::jthread([&,index,operation](){
      parallelEvents[index].owner=GetCurrentThreadId();
      CtBackup::BackupOptions own;own.detailedProgress=RecordDetail;own.detailedProgressUser=&parallelEvents[index];
      parallelResults[index]=CtBackup::CreateValidated7zBackup(operation/L"data/Sites",operation/L"output/backup.7z",
          CTSPACES_VERSION_TEXT,Record,&parallelEvents[index],own);
    });
  }
  for(auto &worker:workers)worker.join();
  Check(parallelResults[0].ok(),"First concurrent isolated backup completes and verifies");
  Check(parallelResults[1].ok(),"Second concurrent isolated backup completes and verifies");
  Check(OnlyClient(root/L"parallel-0/output/backup.7z",L"Parallel Alpha") && OnlyClient(root/L"parallel-1/output/backup.7z",L"Parallel Beta"),
      "Concurrent backup archives retain their separate selected client identities");
  Check(!parallelEvents[0].details.empty() && !parallelEvents[1].details.empty() &&
      std::none_of(parallelEvents[0].values.begin(),parallelEvents[0].values.end(),[&](const Event &e){return e.thread==parallelEvents[1].owner;}) &&
      std::none_of(parallelEvents[1].values.begin(),parallelEvents[1].values.end(),[&](const Event &e){return e.thread==parallelEvents[0].owner;}),
      "Concurrent backup progress never crosses callback recipients");
  std::wcout<<L"SUBSET_FIXTURE="<<root<<L'\n';
}
void RealSdkCase(const fs::path &root) {
  if (fs::exists(root)) throw std::runtime_error("Real progress fixture must be new");
  const auto source = root / L"data/Sites/Progress QA/Profile";
  fs::create_directories(source);
  const auto payload = source / L"payload.bin";
  constexpr size_t blockBytes = 1024u * 1024u;
  constexpr unsigned blocks = 320;
  std::vector<char> block(blockBytes);
  unsigned random = 0x633016u;
  for (auto &value : block) { random = random * 1664525u + 1013904223u; value = static_cast<char>(random >> 24); }
  {
    std::ofstream output(payload, std::ios::binary);
    for (unsigned index = 0; index < blocks; ++index) output.write(block.data(), block.size());
    if (!output) throw std::runtime_error("Real progress source write failed");
  }
  Events events;
  const auto start = std::chrono::steady_clock::now();
  const auto archive = root / L"backup.7z";
  CtBackup::BackupOptions options;options.detailedProgress=RecordDetail;options.detailedProgressUser=&events;
  const auto result = CtBackup::CreateValidated7zBackup(root / L"data/Sites", archive,
      CTSPACES_VERSION_TEXT, Record, &events,options);
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  const auto intermediate = std::count_if(events.values.begin(), events.values.end(), [](const Event &event) { return event.operation == _7zOp::Compress && event.percent > 0 && event.percent < 100; });
  const auto worker = std::count_if(events.values.begin(), events.values.end(), [&](const Event &event) { return event.operation == _7zOp::Compress && event.percent > 0 && event.percent < 100 && event.thread != events.owner; });
  std::cout << "REAL_SDK source_bytes=" << fs::file_size(payload) << " archive_bytes=" << (result.ok() ? fs::file_size(archive) : 0)
      << " seconds=" << seconds << " reports=" << events.values.size() << " intermediate_compress=" << intermediate
      << " worker_intermediate_compress=" << worker << " owner=" << events.owner << " result=" << std::hex << result.code << std::dec << '\n';
  if (!result.ok()) std::wcout << L"DETAILS=" << result.details << L'\n';
  Check(result.ok(), "Real multiblock SDK backup completes compression, CRC verification and publication");
  Check(intermediate > 0, "Real multiblock compression publishes intermediate progress");
  Check(worker > 0,"Real SDK intermediate compression reports reach the consumer from codec worker threads");
  std::set<_7zProgressPhase> phases;
  for(const auto &event:events.details)if(phases.insert(event.phase).second)
      std::cout<<"PHASE value="<<static_cast<int>(event.phase)<<" first_seconds="<<event.seconds<<" owner_thread="<<(event.thread==events.owner)<<'\n';
  Check(phases.size()==6,"A complete verified backup reports all six preparation, scan, compression, recheck, verification and publication phases");
  bool unchanged=fs::file_size(payload)==blockBytes*blocks;
  std::vector<char> actual(blockBytes);
  std::ifstream original(payload,std::ios::binary);
  for(unsigned index=0;index<blocks && unchanged;++index){original.read(actual.data(),actual.size());unchanged=original.good() && actual==block;}
  Check(unchanged,"Real progress probe leaves every synthetic payload byte unchanged");
  std::wcout << L"FIXTURE=" << root << L'\n';
}
}
int wmain(int argc, wchar_t **argv) {
  try {
    if (argc == 2 && !wcscmp(argv[1], L"--bridge")) BridgeCases();
    else if(argc==3 && !wcscmp(argv[1],L"--functional")){BridgeCases();SubsetCases(fs::absolute(argv[2]));}
    else if (argc == 3 && !wcscmp(argv[1], L"--real-sdk")) RealSdkCase(fs::absolute(argv[2]));
    else return 2;
    std::cout << "Backup progress checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
  } catch (const std::exception &error) {
    std::cerr << "Backup progress probe error: " << error.what() << '\n';
    return 2;
  }
}
