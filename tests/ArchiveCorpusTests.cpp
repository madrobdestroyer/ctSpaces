#include "../BackupEngine.h"
#include "../version.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace fs=std::filesystem;
namespace {
unsigned checks=0,failures=0,randomState=0xD13A47B9u;
unsigned Random(){randomState=randomState*1664525u+1013904223u;return randomState;}
void Check(bool condition,const char* description){++checks;if(!condition){++failures;std::cerr<<"FAIL "<<description<<'\n';}}
void Write(const fs::path& path,const std::vector<unsigned char>& bytes){
  fs::create_directories(path.parent_path());std::ofstream stream(path,std::ios::binary);
  stream.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
  if(!stream)throw std::runtime_error("Fixture write failed");
}
std::vector<unsigned char> Read(const fs::path& path){std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error("Fixture read failed");return {std::istreambuf_iterator<char>(stream),{}};}
bool Exclusive(const fs::path& path){HANDLE file=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(file==INVALID_HANDLE_VALUE)return false;CloseHandle(file);return true;}
DWORD Handles(){DWORD count=0;if(!GetProcessHandleCount(GetCurrentProcess(),&count))throw std::runtime_error("Process handle count unavailable");return count;}
void CheckPayload(const fs::path& payload,const std::vector<std::vector<unsigned char>>& sources){
  for(unsigned index=0;index<sources.size();++index)Check(Read(payload/L"Alpha"/(std::to_wstring(index)+L".bin"))==sources[index],"A successful restore contains exact selected source bytes");
}
void CheckRetry(const fs::path& root,const fs::path& valid,unsigned ordinal,const std::vector<std::vector<unsigned char>>& sources){
  Check(SUCCEEDED(_7zTest7z(valid.c_str(),nullptr,nullptr)),"A normal CRC retry succeeds after rejected or interrupted operations");
  CtBackup::StagedRestore retry;const auto stage=root/(L"retry-"+std::to_wstring(ordinal));
  const auto result=CtBackup::StageValidated7zRestore(valid,stage,nullptr,nullptr,retry);
  Check(result.ok(),"A normal validated restore retry succeeds in the same process");if(result.ok())CheckPayload(retry.payloadDir,sources);
  Check(Exclusive(valid),"A successful retry releases the selected archive lease");
}
struct Interruption {unsigned phase=0,calls=0;bool reached=false;_7zOp wanted=_7zOp::Extract;unsigned at=0;};
void Interrupt(void* user,_7zOp operation,unsigned percent,const wchar_t* item){
  auto& state=*static_cast<Interruption*>(user);if(operation!=state.wanted)return;++state.calls;
  const bool wanted=state.phase==0?percent==0:state.phase==1?percent>0&&percent<100&&item&&*item:percent==100&&!item;
  if(!state.reached && wanted){state.reached=true;state.at=percent;std::cout<<"THROW_POINT phase="<<state.phase<<" percent="<<percent<<" item="<<(item&&*item?"named":"empty")<<'\n';throw std::runtime_error("Deterministic scoped progress interruption");}
}
void TestRepeatedInterruptions(const fs::path& root,const fs::path& valid,const std::vector<std::vector<unsigned char>>& sources){
  const auto original=Read(valid);unsigned reached=0,unsupported=0,ordinal=1000;
  for(unsigned api=0;api<4;++api)for(unsigned phase=0;phase<3;++phase)for(unsigned iteration=0;iteration<4;++iteration){
    const unsigned id=ordinal++;Interruption state;state.phase=phase;if(api==3)state.wanted=_7zOp::Compress;
    std::cout<<"INTERRUPT_BEGIN api="<<api<<" phase="<<phase<<" iteration="<<iteration<<'\n';
    const auto output=root/(L"interrupt-"+std::to_wstring(id));HRESULT hr=S_OK;bool threw=false;CtBackup::Result result{};
    try{
      if(api==0)hr=_7zTest7z(valid.c_str(),Interrupt,&state);
      else if(api==1)hr=_7zExtra_7z(valid.c_str(),output.c_str(),Interrupt,&state);
      else if(api==2){CtBackup::StagedRestore staged;result=CtBackup::StageValidated7zRestore(valid,output,Interrupt,&state,staged);Check(!state.reached||(!staged.validation&&staged.stagingRoot.empty()),"An interrupted validated restore never returns a commit-ready seal");}
      else{const auto destination=output.wstring()+L".7z";Write(destination,original);result=CtBackup::CreateValidated7zBackup(root/L"d/Sites",destination,CTSPACES_VERSION_TEXT,Interrupt,&state);Check(!state.reached||Read(destination)==original,"Interrupted compression preserves the previous destination bytes");}
    }catch(const std::exception&){threw=true;}
    if(state.reached){++reached;Check(threw||FAILED(hr)||!result.ok(),"An actual progress interruption fails the operation");}
    else{++unsupported;Check(phase==1,"Required early and completion progress boundaries are reachable");}
    Check(Read(valid)==original&&Exclusive(valid),"Every interruption preserves and releases the selected archive immediately");
    for(unsigned index=0;index<sources.size();++index)Check(Exclusive(root/L"d/Sites/Alpha"/(std::to_wstring(index)+L".bin")),"An interrupted operation releases each source-client read lease");
    if(api==2&&state.reached)Check(!fs::exists(output),"Interrupted validated extraction cleans only its owned staging tree");
    if(iteration==3)CheckRetry(root,valid,id,sources);
    std::cout<<"INTERRUPT api="<<api<<" phase="<<phase<<" iteration="<<iteration<<" reached="<<state.reached<<" percent="<<state.at<<" callbacks="<<state.calls<<'\n';
  }
  std::cout<<"Interruption cases=48 reached="<<reached<<" unsupported-middle="<<unsupported<<'\n';
}
void TestCorpus(const fs::path& root,const fs::path& valid,const std::vector<std::vector<unsigned char>>& sources){
  const auto original=Read(valid);unsigned accepted=0,rejected=0;
  for(unsigned index=0;index<80;++index){
    auto bytes=original;
    if(index<32)bytes.resize(Random()%bytes.size());
    else if(index<48)bytes[index-32]^=static_cast<unsigned char>(1u<<(Random()%8));
    else bytes[Random()%bytes.size()]^=static_cast<unsigned char>(1u<<(Random()%8));
    const auto archive=root/(L"mutated-"+std::to_wstring(index)+L".7z"),stage=root/(L"mutation-stage-"+std::to_wstring(index));Write(archive,bytes);
    _7zArchiveInfo information{};const HRESULT inspected=_7zInspect7z(archive.c_str(),&information),tested=_7zTest7z(archive.c_str(),nullptr,nullptr);
    CtBackup::StagedRestore staged;const auto result=CtBackup::StageValidated7zRestore(archive,stage,nullptr,nullptr,staged);
    if(result.ok()){++accepted;Check(SUCCEEDED(inspected)&&SUCCEEDED(tested)&&staged.validation!=nullptr,"A corpus input accepted for restore passed metadata, CRC and ownership validation");CheckPayload(staged.payloadDir,sources);}
    else{++rejected;Check(!staged.validation&&staged.stagingRoot.empty()&&!fs::exists(stage),"A rejected malformed stream leaves no commit seal or owned stage");}
    Check(Read(archive)==bytes&&Exclusive(archive),"Malformed-stream operations preserve and release exact input bytes");
    if(index%10==9)CheckRetry(root,valid,index,sources);
    std::cout<<"CORPUS index="<<index<<" kind="<<(index<32?"truncate":index<48?"header-bit":"stream-bit")<<" bytes="<<bytes.size()<<" inspect="<<std::hex<<inspected<<" test="<<tested<<" stage="<<result.code<<std::dec<<'\n';
  }
  std::cout<<"Corpus cases=80 accepted="<<accepted<<" rejected="<<rejected<<'\n';
}
}
int wmain(int argc,wchar_t** argv){
  try{
    SetErrorMode(SEM_NOGPFAULTERRORBOX|SEM_FAILCRITICALERRORS|SEM_NOOPENFILEERRORBOX);
    std::cout<<std::unitbuf;std::cerr<<std::unitbuf;
    if(argc!=2)return 2;const auto root=fs::absolute(argv[1]);if(fs::exists(root))throw std::runtime_error("New fixture root required");
    HANDLE job=CreateJobObjectW(nullptr,nullptr);struct CloseJob{HANDLE value;~CloseJob(){if(value)CloseHandle(value);}} closeJob{job};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_PROCESS_MEMORY;limits.ProcessMemoryLimit=512ull*1024ull*1024ull;
    if(!job||!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))||!AssignProcessToJobObject(job,GetCurrentProcess()))throw std::runtime_error("Required 512MiB corpus process memory cap could not be installed");
    std::vector<std::vector<unsigned char>> sources(3,std::vector<unsigned char>(65536));
    for(unsigned index=0;index<sources.size();++index){for(auto& byte:sources[index])byte=static_cast<unsigned char>(Random()>>24);Write(root/L"d/Sites/Alpha"/(std::to_wstring(index)+L".bin"),sources[index]);}
    const auto valid=root/L"selected.7z";Check(CtBackup::CreateValidated7zBackup(root/L"d/Sites",valid,CTSPACES_VERSION_TEXT,nullptr,nullptr).ok(),"Corpus fixture creates a genuine validated backup");
    CheckRetry(root,valid,9999,sources);const DWORD baseline=Handles();
    TestRepeatedInterruptions(root,valid,sources);const DWORD afterInterruptions=Handles();TestCorpus(root,valid,sources);const DWORD afterCorpus=Handles();
    for(unsigned index=0;index<sources.size();++index)Check(Read(root/L"d/Sites/Alpha"/(std::to_wstring(index)+L".bin"))==sources[index],"All repeated adversarial operations preserve exact original client bytes");
    Check(afterInterruptions<=baseline+2&&afterCorpus<=baseline+2,"Same-process native handle counts remain bounded after repeated failures and retries");
    std::cout<<"Process handles baseline="<<baseline<<" after-interruptions="<<afterInterruptions<<" after-corpus="<<afterCorpus<<'\n';
    std::cout<<"Archive corpus checks="<<checks<<" failures="<<failures<<" seed=0xD13A47B9\n";std::wcout<<L"FIXTURE="<<root<<L'\n';return failures?1:0;
  }catch(const std::exception& error){std::cerr<<"Archive corpus fixture error: "<<error.what()<<'\n';return 2;}
}
