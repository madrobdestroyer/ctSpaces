#include "../BackupEngine.h"
#include "../version.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace fs=std::filesystem;
namespace {
unsigned checks=0, failures=0;
void Check(bool value,const char* message) {
  ++checks;std::cout<<(value?"PASS ":"FAIL ")<<message<<'\n';if(!value)++failures;
}
void Write(const fs::path& path,const std::vector<unsigned char>& value) {
  fs::create_directories(path.parent_path());std::ofstream stream(path,std::ios::binary);
  stream.write(reinterpret_cast<const char*>(value.data()),value.size());
  if(!stream)throw std::runtime_error("Private fixture write failed");
}
std::vector<unsigned char> Read(const fs::path& path) {
  std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error("Private fixture read failed");
  return {std::istreambuf_iterator<char>(stream),{}};
}
bool Exclusive(const fs::path& path) {
  HANDLE handle=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(handle==INVALID_HANDLE_VALUE)return false;CloseHandle(handle);return true;
}
struct Interrupt { bool reached=false;_7zOp operation=_7zOp::Extract; };
void InterruptAtCompletion(void* user,_7zOp operation,unsigned percent,const wchar_t* item) {
  auto& state=*static_cast<Interrupt*>(user);
  if(!state.reached && operation==state.operation && percent==100 && !item) {
    state.reached=true;throw std::runtime_error("Synthetic progress interruption after completion");
  }
}
void TestLowLevelInterruptions(const fs::path& root,const fs::path& valid) {
  for(unsigned mode=0;mode<2;++mode) {
    const auto archive=root/(mode?L"extract.7z":L"test.7z");fs::copy_file(valid,archive);
    Interrupt state;bool threw=false;HRESULT result=S_OK;
    try {result=mode?_7zExtra_7z(archive.c_str(),(root/L"generic-output").c_str(),InterruptAtCompletion,&state):
      _7zTest7z(archive.c_str(),InterruptAtCompletion,&state);}catch(const std::exception&){threw=true;}
    Check(state.reached && (threw || FAILED(result)),mode?"Generic extraction completion interruption was reached and failed":"Generic CRC-test completion interruption was reached and failed");
    Check(Exclusive(archive),mode?"Interrupted generic extraction releases its archive lease immediately":"Interrupted generic CRC test releases its archive lease immediately");
    Check(SUCCEEDED(_7zTest7z(archive.c_str(),nullptr,nullptr)),"A fresh integrity retry accepts the unchanged selected archive");
    if(mode)Check(Read(root/L"generic-output/Sites/Alpha/state.bin")==Read(root/L"d/Sites/Alpha/state.bin"),"Interrupted completion leaves exact extracted source bytes available");
  }
}
void TestTransactionInterruptions(const fs::path& root,const fs::path& valid) {
  const auto original=Read(valid);
  for(unsigned mode=0;mode<2;++mode) {
    const auto output=root/(mode?L"verify-interrupt.7z":L"compress-interrupt.7z");Write(output,original);
    Interrupt state;state.operation=mode?_7zOp::Extract:_7zOp::Compress;
    const auto result=CtBackup::CreateValidated7zBackup(root/L"d/Sites",output,CTSPACES_VERSION_TEXT,InterruptAtCompletion,&state);
    Check(state.reached && !result.ok(),mode?"Backup verification completion interruption fails closed":"Backup compression completion interruption fails closed");
    Check(Read(output)==original && Exclusive(output),"Interrupted backup preserves and unlocks exact previous destination bytes");
    Check(CtBackup::CreateValidated7zBackup(root/L"d/Sites",output,CTSPACES_VERSION_TEXT,nullptr,nullptr).ok(),"A normal backup retries successfully after the interruption");
  }
  Interrupt state;CtBackup::StagedRestore rejected;const auto stage=root/L"interrupted-stage";
  const auto result=CtBackup::StageValidated7zRestore(valid,stage,InterruptAtCompletion,&state,rejected);
  Check(state.reached && !result.ok() && !rejected.validation && rejected.stagingRoot.empty(),"Interrupted validated restore returns no commit-ready seal");
  Check(!fs::exists(stage) && Exclusive(valid) && Read(valid)==original,"Interrupted validated restore removes owned output and releases unchanged source");
  CtBackup::StagedRestore retry;
  Check(CtBackup::StageValidated7zRestore(valid,stage,nullptr,nullptr,retry).ok(),"Validated restore retries at the same stage after interruption");
}
void TestMalformedStreams(const fs::path& root,const fs::path& valid) {
  const auto bytes=Read(valid);unsigned cases=0;
  for(const size_t length:{size_t{0},size_t{1},size_t{6},size_t{16},size_t{31},size_t{32},bytes.size()/2,bytes.size()-1}) {
    const auto archive=root/(L"truncated-"+std::to_wstring(cases)+L".7z"),stage=root/(L"truncated-stage-"+std::to_wstring(cases));
    Write(archive,{bytes.begin(),bytes.begin()+length});const auto before=Read(archive);CtBackup::StagedRestore rejected;
    const auto result=CtBackup::StageValidated7zRestore(archive,stage,nullptr,nullptr,rejected);
    Check(!result.ok() && !rejected.validation && rejected.stagingRoot.empty(),"Truncated header/payload is never a commit-ready restore");
    Check(Read(archive)==before && Exclusive(archive) && !fs::exists(stage),"Malformed stream rejection preserves source and leaves no bound stage or archive lease");++cases;
  }
  std::cout<<"Malformed stream cases="<<cases<<'\n';
}
}
int wmain(int argc,wchar_t** argv) {
  try {
    if(argc!=2)return 2;
    const auto root=fs::absolute(argv[1]);
    if(fs::exists(root))throw std::runtime_error("Fixture root must be new");
    fs::create_directories(root/L"d/Sites/Alpha");
    std::vector<unsigned char> bytes(32768);unsigned random=0x5A91D3u;
    for(auto& byte:bytes){random=random*1664525u+1013904223u;byte=static_cast<unsigned char>(random>>24);}
    Write(root/L"d/Sites/Alpha/state.bin",bytes);const auto valid=root/L"selected.7z";
    Check(CtBackup::CreateValidated7zBackup(root/L"d/Sites",valid,CTSPACES_VERSION_TEXT,nullptr,nullptr).ok(),"Real SDK creates a selected backup from isolated synthetic client data");
    TestLowLevelInterruptions(root,valid);TestTransactionInterruptions(root,valid);TestMalformedStreams(root,valid);
    Check(Read(root/L"d/Sites/Alpha/state.bin")==bytes,"All adversarial cases preserve original client bytes exactly");
    std::cout<<"Archive interruption checks="<<checks<<" failures="<<failures<<" seed=0x5A91D3\n";
    std::wcout<<L"FIXTURE="<<root<<L'\n';return failures?1:0;
  }catch(const std::exception& error){std::cerr<<"Archive interruption fixture error: "<<error.what()<<'\n';return 2;}
}
