// Actual backend, decoder and ownership with test-only stream and attribute
// result simulations. No disk exhaustion or native SDK WriteFile interception.
#include "RestoreOutputFailureStreamBoundary.h"
#include "../BackupEngine.h"
#include "../version.h"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifndef CTSPACES_RESTORE_OUTPUT_FIXTURE_PARENT
#error Bind this test to a fresh workspace artifact directory through its wrapper.
#endif
#ifdef CTSPACES_ARCHIVE_ITEM_LIMIT_FOR_TEST
#error Production archive limits are required.
#endif
namespace fs = std::filesystem;
namespace {
constexpr unsigned long long kBudget=2ull*1024ull*1024ull;
unsigned checks=0,cases=0;
void Check(bool value,const char*description){++checks;std::cout<<(value?"PASS ":"FAIL ")<<description<<std::endl;if(!value)throw std::runtime_error(description);}
void NoReparse(const fs::path&path){for(auto current=path;!current.empty();){const auto attributes=GetFileAttributesW(current.c_str());if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Reparse fixture path refused");const auto parent=current.parent_path();if(parent==current)break;current=parent;}}
fs::path Reserve(const wchar_t*requested){const auto parent=fs::path(CTSPACES_RESTORE_OUTPUT_FIXTURE_PARENT).lexically_normal();const fs::path raw(requested);if(!raw.is_absolute()||!parent.is_absolute())throw std::runtime_error("Absolute fixture path required");for(const auto&part:raw)if(part==L"."||part==L"..")throw std::runtime_error("Fixture dot component refused");const auto root=raw.lexically_normal();if(_wcsicmp(root.parent_path().c_str(),parent.c_str())||root.filename().empty()||root.filename().native().size()>12)throw std::runtime_error("Fresh direct compile-bound fixture child required");NoReparse(root);if(GetFileAttributesW(root.c_str())!=INVALID_FILE_ATTRIBUTES||!CreateDirectoryW(root.c_str(),nullptr))throw std::runtime_error("Existing fixture refused");return root;}
void WriteNew(const fs::path&path,const std::string&bytes){if(bytes.size()>8192)throw std::runtime_error("Fixture file exceeds eight KiB");NoReparse(path);fs::create_directories(path.parent_path());NoReparse(path);HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Private CREATE_NEW failed");DWORD wrote=0;const bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&wrote,nullptr)&&wrote==bytes.size()&&FlushFileBuffers(file);CloseHandle(file);if(!ok)throw std::runtime_error("Private fixture write failed");}
std::string Read(const fs::path&path){if(fs::file_size(path)>kBudget)throw std::runtime_error("Private inventory file exceeds budget");std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("Private fixture read failed");return{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};}
unsigned long long Time(FILETIME value){return(static_cast<unsigned long long>(value.dwHighDateTime)<<32)|value.dwLowDateTime;}
struct Entry{bool directory=false;DWORD volume=0,high=0,low=0,attributes=0;unsigned long long creation=0,write=0;std::string bytes;bool operator==(const Entry&)const=default;};
struct Tree{bool exists=false;std::map<std::wstring,Entry> entries;bool operator==(const Tree&)const=default;};
Entry Capture(const fs::path&path){NoReparse(path);HANDLE file=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Private inventory binding failed");BY_HANDLE_FILE_INFORMATION info{};const bool ok=GetFileInformationByHandle(file,&info)&&GetFileType(file)==FILE_TYPE_DISK&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&((info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||info.nNumberOfLinks==1);CloseHandle(file);if(!ok)throw std::runtime_error("Unsafe private inventory object refused");Entry item;item.directory=(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;item.volume=info.dwVolumeSerialNumber;item.high=info.nFileIndexHigh;item.low=info.nFileIndexLow;item.attributes=info.dwFileAttributes;item.creation=Time(info.ftCreationTime);item.write=Time(info.ftLastWriteTime);if(!item.directory)item.bytes=Read(path);return item;}
Tree Snapshot(const fs::path&path){NoReparse(path);const auto attributes=GetFileAttributesW(path.c_str());if(attributes==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)return{};throw std::runtime_error("Private snapshot root unavailable");}Tree tree;tree.exists=true;tree.entries.emplace(L"",Capture(path));if(attributes&FILE_ATTRIBUTE_DIRECTORY)for(const auto&child:fs::recursive_directory_iterator(path))tree.entries.emplace(child.path().lexically_relative(path).native(),Capture(child.path()));return tree;}
bool SamePayload(const Tree&left,const Tree&right){if(!left.exists||!right.exists||left.entries.size()!=right.entries.size())return false;for(const auto&[name,item]:left.entries){const auto found=right.entries.find(name);if(found==right.entries.end()||item.directory!=found->second.directory||item.bytes!=found->second.bytes||item.creation!=found->second.creation||item.write!=found->second.write)return false;}return true;}
unsigned long long PhysicalBytes(const fs::path&root){unsigned long long bytes=0;for(const auto&[name,item]:Snapshot(root).entries){(void)name;if(item.bytes.size()>kBudget-bytes)throw std::runtime_error("Fixture budget exceeded");bytes+=item.bytes.size();}return bytes;}
bool Cleared(const CtBackup::StagedRestore&staged){return staged.stagingRoot.empty()&&staged.payloadDir.empty()&&!staged.profileCount&&!staged.validation;}
void Status(const wchar_t*label,const CtBackup::Result&result){std::wcout<<label<<L"=0x"<<std::hex<<std::setw(8)<<std::setfill(L'0')<<static_cast<unsigned long>(result.code)<<std::dec<<L"\nDETAILS="<<result.details<<L'\n';}
std::wstring Normalized(fs::path path){auto value=path.lexically_normal().native();if(value.starts_with(L"\\\\?\\"))value.erase(0,4);for(auto&ch:value)if(ch==L'/')ch=L'\\';return value;}
}

namespace restore_output_boundary {
enum class Fault{None,PositiveShort,PartialDiskFull,ZeroProgress,CloseFailure,ForeignDiskFull};
struct Observation{Fault fault=Fault::None;fs::path target,foreign;unsigned hits=0,targetWrites=0,targetCloses=0;UInt32 requested=0,partialBytes=0;BY_HANDLE_FILE_INFORMATION identity{};bool hasIdentity=false;Tree foreignBefore;};
struct AttributeObservation {
 fs::path target;
 DWORD mask=0;
 bool directory=false;
 unsigned hits=0;
 BY_HANDLE_FILE_INFORMATION identity{};
};
std::mutex mutex;
Observation state;
AttributeObservation attributeState;
std::atomic<unsigned> liveWrappers=0;
void WrapperCreated()noexcept{liveWrappers.fetch_add(1);}
void WrapperDestroyed()noexcept{liveWrappers.fetch_sub(1);}
bool Target(HANDLE handle){
 if(state.fault==Fault::None||handle==INVALID_HANDLE_VALUE)return false;
 wchar_t finalPath[32768]{};const auto length=GetFinalPathNameByHandleW(handle,finalPath,static_cast<DWORD>(std::size(finalPath)),FILE_NAME_NORMALIZED);
 BY_HANDLE_FILE_INFORMATION info{};
 if(!length||length>=std::size(finalPath)||!GetFileInformationByHandle(handle,&info)||GetFileType(handle)!=FILE_TYPE_DISK||
    (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||info.nNumberOfLinks!=1||
    _wcsicmp(Normalized(finalPath).c_str(),Normalized(state.target).c_str()))return false;
 if(state.hasIdentity&&(info.dwVolumeSerialNumber!=state.identity.dwVolumeSerialNumber||info.nFileIndexHigh!=state.identity.nFileIndexHigh||info.nFileIndexLow!=state.identity.nFileIndexLow))return false;
 state.identity=info;state.hasIdentity=true;return true;
}
void Arm(Fault fault,const fs::path&target,const fs::path&foreign={}){NoReparse(target);if(!foreign.empty())NoReparse(foreign);std::lock_guard<std::mutex> lock(mutex);state={};state.fault=fault;state.target=fs::absolute(target).lexically_normal();state.foreign=foreign;}
Observation Observe(){std::lock_guard<std::mutex> lock(mutex);return state;}
void Disarm(){std::lock_guard<std::mutex> lock(mutex);state={};}
HRESULT Write(COutFileStream*native,const void*data,UInt32 bytes,UInt32*processed)noexcept{
 try{
  std::lock_guard<std::mutex> lock(mutex);
  const bool target=Target(native->File.GetHandle());if(target)++state.targetWrites;
  if(target&&!state.hits&&bytes&&state.fault!=Fault::CloseFailure){
   ++state.hits;state.requested=bytes;if(processed)*processed=0;
   if(state.fault==Fault::ZeroProgress)return S_OK;
   const UInt32 partial=state.fault==Fault::PositiveShort?(std::max)(UInt32{1},bytes/2):(std::min)(UInt32{17},bytes);
   UInt32 actual=0;const HRESULT result=static_cast<IOutStream*>(native)->Write(data,partial,&actual);state.partialBytes=actual;if(processed)*processed=actual;
   if(FAILED(result))return result;
   if(state.fault==Fault::ForeignDiskFull){WriteNew(state.foreign,"Independent foreign staging sentinel\r\n");state.foreignBefore=Snapshot(state.foreign);}
   return state.fault==Fault::PositiveShort?S_OK:HRESULT_FROM_WIN32(ERROR_DISK_FULL);
  }
  return static_cast<IOutStream*>(native)->Write(data,bytes,processed);
 }catch(const std::bad_alloc&){if(processed)*processed=0;return E_OUTOFMEMORY;}catch(...){if(processed)*processed=0;return E_FAIL;}
}
HRESULT Close(COutFileStream*native)noexcept{
 try{std::lock_guard<std::mutex> lock(mutex);const bool target=Target(native->File.GetHandle());if(target)++state.targetCloses;const HRESULT closed=native->Close();if(target&&state.fault==Fault::CloseFailure&&!state.hits&&closed==S_OK){++state.hits;return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);}return closed;}catch(...){return E_FAIL;}
}
void ArmAttributeRefusal(const fs::path& target,DWORD mask,bool directory){
 NoReparse(target);
 if(fs::exists(target))throw std::runtime_error("The private attribute fault target must be absent before extraction");
 std::lock_guard<std::mutex> lock(mutex);
 attributeState={target,mask,directory};
}
AttributeObservation FinishAttributeRefusal(){
 std::lock_guard<std::mutex> lock(mutex);
 auto result=std::move(attributeState);
 attributeState={};
 return result;
}
bool ApplyAttributes(CFSTR path,DWORD attributes)noexcept{
 bool refuse=false;
 try{
  std::lock_guard<std::mutex> lock(mutex);
  if(!attributeState.target.empty()&&!attributeState.hits&&path&&
     (attributes&attributeState.mask)==attributeState.mask&&
     !_wcsicmp(Normalized(path).c_str(),Normalized(attributeState.target).c_str())){
   const HANDLE file=CreateFileW(path,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
       nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
   if(file!=INVALID_HANDLE_VALUE){
    auto& identity=attributeState.identity;
    refuse=GetFileInformationByHandle(file,&identity)&&GetFileType(file)==FILE_TYPE_DISK&&
        !(identity.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&
        ((identity.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0)==attributeState.directory&&
        (attributeState.directory||identity.nNumberOfLinks==1);
    CloseHandle(file);
    if(refuse)++attributeState.hits;
   }
  }
 }catch(...){}
 if(refuse){
  SetLastError(ERROR_ACCESS_DENIED);
  return false;
 }
 return NWindows::NFile::NDir::SetFileAttrib_PosixHighDetect(path,attributes);
}
}

namespace {
void RunAttributes(const fs::path& root,bool before){
 const auto source=root/L"s/Sites",archive=root/L"input.7z";
 const auto privateDir=fs::path(L"Alpha/PrivateBox"),privateFile=privateDir/L"note.ctn";
 const std::string incoming="exact attributes fixture notebook\r\n";
 constexpr DWORD fileMask=FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM|FILE_ATTRIBUTE_READONLY;
 constexpr DWORD directoryMask=FILE_ATTRIBUTE_HIDDEN;
 WriteNew(source/privateFile,incoming);
 Check(SetFileAttributesW((source/privateDir).c_str(),directoryMask)!=FALSE&&
       SetFileAttributesW((source/privateFile).c_str(),fileMask)!=FALSE,
       "An ordinary source contains a hidden directory and a hidden system read-only file");
 const auto sourceBefore=Snapshot(source);
 Check(CtBackup::CreateValidated7zBackup(source,archive,CTSPACES_VERSION_TEXT,nullptr,nullptr).ok()&&
       _7zTest7z(archive.c_str(),nullptr,nullptr)==S_OK,
       "Actual backup creates a CRC-valid archive of the fixture attributes");
 const auto archiveBefore=Snapshot(archive);
 for(const bool directory:{false,true}){
  ++cases;
  const auto run=root/(directory?L"dir":L"file");
  const auto stage=run/L"stage",live=run/L"l/Sites",recovery=run/L"l/R";
  const auto relative=directory?privateDir:privateFile;
  const auto target=stage/L"Sites"/relative;
  const DWORD mask=directory?directoryMask:fileMask;
  WriteNew(live/L"Previous/old.ctn","exact previous data before attribute refusal\r\n");
  const auto liveBefore=Snapshot(live);
  restore_output_boundary::ArmAttributeRefusal(target,mask,directory);
  CtBackup::StagedRestore staged;
  const auto result=CtBackup::StageValidated7zRestore(archive,stage,nullptr,nullptr,staged);
  const auto observed=restore_output_boundary::FinishAttributeRefusal();
  Status(directory?L"RESTORE_ATTR_DIRECTORY_HRESULT":L"RESTORE_ATTR_FILE_HRESULT",result);
  Check(observed.hits==1&&observed.directory==directory&&observed.target==target,
        "The one-shot attribute refusal reaches only the exact private output object");
  if(before){
   Check(result.code==S_OK&&staged.validation&&staged.profileCount==1,
         "The baseline incorrectly seals extraction despite the attribute write refusal");
   const auto incomplete=Capture(target);
   Check((incomplete.attributes&mask)==0&&incomplete.volume==observed.identity.dwVolumeSerialNumber&&
         incomplete.high==observed.identity.nFileIndexHigh&&incomplete.low==observed.identity.nFileIndexLow,
         "The baseline retains and seals the exact output object with missing archived attributes");
  }else{
   Check(result.code==HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED)&&Cleared(staged)&&
         result.details.find(L"attributes")!=std::wstring::npos&&
         result.details.find(relative.filename().native())!=std::wstring::npos,
         "The repaired restore surfaces the exact HRESULT and affected archive item without a seal");
   const auto remaining=Snapshot(stage);
   Check(!remaining.exists||(remaining.entries.size()==1&&remaining.entries.begin()->second.directory),
         "Failed attribute application disposes the owned unverified output");
  }
  Check(Snapshot(source)==sourceBefore&&Snapshot(archive)==archiveBefore&&Snapshot(live)==liveBefore&&
        !Snapshot(recovery).exists&&!restore_output_boundary::liveWrappers.load(),
        "Attribute refusal leaves the exact independent source, archive and live collection intact");
  const auto retry=before?run/L"retry":stage;
  const auto retried=CtBackup::StageValidated7zRestore(archive,retry,nullptr,nullptr,staged);
  Check(retried.ok()&&staged.validation&&staged.profileCount==1&&
        Read(staged.payloadDir/privateFile)==incoming&&
        (Capture(staged.payloadDir/privateDir).attributes&directoryMask)==directoryMask&&
        (Capture(staged.payloadDir/privateFile).attributes&fileMask)==fileMask,
        "The same archive stages both exact attribute sets and bytes after disarming the refusal");
  const auto verified=Snapshot(staged.payloadDir);
  const auto committed=CtBackup::CommitStagedRestore(staged,live,recovery);
  Check(committed.ok()&&committed.details.empty()&&Snapshot(live)==verified&&
        Snapshot(recovery)==liveBefore&&!Snapshot(retry).exists,
        "Retry installs the exact verified payload, retains the previous collection and removes its stage");
  Check(Snapshot(source)==sourceBefore&&Snapshot(archive)==archiveBefore&&
        _7zTest7z(archive.c_str(),nullptr,nullptr)==S_OK&&!restore_output_boundary::liveWrappers.load(),
        "Retry preserves the original attribute-bearing source and archive and closes all stream owners");
 }
 std::cout<<"ATTRIBUTE_REFUSAL_SIMULATION_ONLY=1\nATTRIBUTE_RETRY_COMMITTED=1\n"
          <<"ATTRIBUTE_REFUSAL_MODE="<<(before?"Before":"After")<<'\n';
}

void Run(const fs::path&root){
 const auto source=root/L"s/Sites",archive=root/L"o/b.7z",live=root/L"l/Sites",recovery=root/L"l/R";
 fs::create_directories(archive.parent_path());std::string data(8192,'\0');unsigned random=0x635031u;for(auto&byte:data){random^=random<<13;random^=random>>17;random^=random<<5;byte=static_cast<char>(random>>8);}
 WriteNew(source/L"Alpha/state.bin",data);WriteNew(source/L"Beta/ctSpaces-client-notes.txt","Beta legacy note remains independently owned\r\n");WriteNew(live/L"Previous/old.txt","Previous live client data retained for recovery\r\n");
 const auto sourceBefore=Snapshot(source),liveBefore=Snapshot(live);
 Check(CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(live,L"Alpha")&&CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(live,L"Beta"),"Both fixture client destinations fit production path budgets");
 const auto backup=CtBackup::CreateValidated7zBackup(source,archive,CTSPACES_VERSION_TEXT,nullptr,nullptr);Check(backup.ok(),"Actual backend creates an ordinary small two-client archive before arming any stream fault");
 _7zArchiveInfo info{};Check(_7zInspect7z(archive.c_str(),&info)==S_OK&&info.sitesProfileCount==2&&_7zTest7z(archive.c_str(),nullptr,nullptr)==S_OK,"Ordinary archive inspection and full SDK CRC pass before output failure tests");
 const auto archiveBefore=Snapshot(archive);
 const auto unchanged=[&](){return Snapshot(source)==sourceBefore&&Snapshot(archive)==archiveBefore&&Snapshot(live)==liveBefore&&!Snapshot(recovery).exists;};
 const auto target=[&](const fs::path&stage){return stage/L"Sites/Alpha/state.bin";};
 using restore_output_boundary::Fault;
 ++cases;
 const auto shortRoot=root/L"short";CtBackup::StagedRestore shortStage;
 restore_output_boundary::Arm(Fault::PositiveShort,target(shortRoot));
 auto result=CtBackup::StageValidated7zRestore(archive,shortRoot,nullptr,nullptr,shortStage);const auto shortObserved=restore_output_boundary::Observe();restore_output_boundary::Disarm();Status(L"POSITIVE_SHORT_STREAM",result);
 Check(shortObserved.hits==1&&shortObserved.hasIdentity&&shortObserved.partialBytes>0&&shortObserved.partialBytes<shortObserved.requested&&shortObserved.targetWrites>=2,"The actual decoder retries after one exact-target stream call performs a positive partial native write");
 Check(result.ok()&&shortStage.validation&&shortStage.profileCount==2&&SamePayload(sourceBefore,Snapshot(shortStage.payloadDir)),"Positive short stream result still stages every byte and timestamp for both independent clients");
 Check(!restore_output_boundary::liveWrappers.load()&&unchanged(),"Positive short handling closes wrapper owners and preserves source/archive/live identities");
 const auto shortBefore=Snapshot(shortRoot);
 fs::path foreignRoot,foreignFile,zeroLivePath,zeroRecoveryPath;Tree retainedForeign,zeroInstalled,zeroPrevious;
 std::string zeroOutcome;
 for(const auto fault:{Fault::PartialDiskFull,Fault::ZeroProgress,Fault::CloseFailure,Fault::ForeignDiskFull}){
  ++cases;const auto label=fault==Fault::PartialDiskFull?L"diskfull":fault==Fault::ZeroProgress?L"zero":fault==Fault::CloseFailure?L"close":L"foreign";
  const auto stage=root/label;const auto sentinel=stage/L"foreign.txt";CtBackup::StagedRestore staged;
  restore_output_boundary::Arm(fault,target(stage),fault==Fault::ForeignDiskFull?sentinel:fs::path{});
  result=CtBackup::StageValidated7zRestore(archive,stage,nullptr,nullptr,staged);const auto observed=restore_output_boundary::Observe();restore_output_boundary::Disarm();Status(label,result);
  std::wcout<<L"SIMULATED_TARGET="<<observed.target<<L"\nHITS="<<observed.hits<<L" REQUESTED="<<observed.requested<<L" ACTUAL_PARTIAL="<<observed.partialBytes<<L" TARGET_WRITES="<<observed.targetWrites<<L" TARGET_CLOSES="<<observed.targetCloses<<L'\n';
  Check(observed.hits==1&&observed.hasIdentity,"Exactly one stream-result fault reaches the checked private regular-file identity");
  // ISequentialOutStream requires positive progress for a nonzero request.
  // A one-shot S_OK/zero result violates that producer contract. The decoder
  // may reject it or recover by retrying; only a complete exact seal is safe.
  if(fault==Fault::ZeroProgress&&result.ok()){
   Check(observed.partialBytes==0&&observed.targetWrites>=2,"The decoder recovers from the one-shot zero-progress contract violation through a later write");
   Check(staged.validation&&staged.profileCount==2&&SamePayload(sourceBefore,Snapshot(staged.payloadDir)),"Recovered zero-progress handling seals only the complete exact two-client payload and timestamps");
   const auto zeroPayload=Snapshot(staged.payloadDir);zeroLivePath=root/L"zl/Sites";zeroRecoveryPath=root/L"zl/R";
   WriteNew(zeroLivePath/L"Previous/old.txt","Independent zero-result recovery sentinel\r\n");zeroPrevious=Snapshot(zeroLivePath);
   result=CtBackup::CommitStagedRestore(staged,zeroLivePath,zeroRecoveryPath);Status(L"ZERO_CONTRACT_RECOVERY_COMMIT",result);
   zeroInstalled=Snapshot(zeroLivePath);
   Check(result.ok()&&zeroInstalled==zeroPayload&&Snapshot(zeroRecoveryPath)==zeroPrevious,"A recovered exact stage commits to its isolated live tree and preserves its own previous collection");
   Check(!restore_output_boundary::liveWrappers.load()&&unchanged()&&Snapshot(shortRoot)==shortBefore,"Zero-result recovery closes stream owners and leaves all independent source/archive/live/valid-stage data intact");
   Check(PhysicalBytes(root)<=kBudget,"The complete retained fixture stays under two MiB after zero-result recovery");
   zeroOutcome="RecoveredExact";continue;
  }
  Check(!result.ok()&&Cleared(staged),"A failed extraction stream result cannot leave a successful or reusable staging seal");
  if(fault==Fault::PartialDiskFull||fault==Fault::ForeignDiskFull){Check(observed.partialBytes==17&&observed.requested>=observed.partialBytes,"The disk-full stream simulation records seventeen bytes actually written through the original SDK stream");Check(result.code==HRESULT_FROM_WIN32(ERROR_DISK_FULL),"Extraction preserves the injected stream disk-full HRESULT");}
  if(fault==Fault::ZeroProgress){Check(observed.partialBytes==0&&observed.targetWrites>=1,"A rejected zero-progress contract violation terminates with no successful staging seal");zeroOutcome="RejectedUnsealed";}
  if(fault==Fault::CloseFailure)Check(result.code==HRESULT_FROM_WIN32(ERROR_WRITE_FAULT)&&result.details.find(L"state.bin")!=std::wstring::npos,"Reported stream-close failure preserves its HRESULT and archive-item diagnostic");
  Check(!restore_output_boundary::liveWrappers.load()&&unchanged()&&Snapshot(shortRoot)==shortBefore,"Stream failure closes original owners and preserves source/archive/live and the separate valid stage");
  Check(!CtBackup::CommitStagedRestore(staged,live,recovery).ok()&&unchanged(),"Direct commit of the failed stream staging output rejects without changing live or recovery data");
  if(fault==Fault::ForeignDiskFull){
   Check(observed.foreignBefore.exists&&Snapshot(sentinel)==observed.foreignBefore,"Ownership cleanup retains exact independently added foreign sentinel bytes and file identity");
   const auto remains=Snapshot(stage);Check(remains.exists&&remains.entries.size()==2&&!Snapshot(stage/L"Sites").exists,"Failed-stage cleanup removes its own payload objects without adopting or recursively deleting the foreign addition");
   result=CtBackup::StageValidated7zRestore(archive,stage,nullptr,nullptr,staged);Check(!result.ok()&&Cleared(staged)&&Snapshot(stage)==remains&&unchanged(),"Retry at the foreign-occupied location refuses without overwriting independent retained data");
   foreignRoot=stage;foreignFile=sentinel;retainedForeign=Snapshot(sentinel);
  }else{const auto remains=Snapshot(stage);Check(!remains.exists||(remains.entries.size()==1&&remains.entries.begin()->second.directory),"Controlled extraction failure removes its owned partial payload rather than leaving an unverified file");}
  Check(PhysicalBytes(root)<=kBudget,"The entire retained ordinary fixture remains under two MiB after each stream case");
 }
 ++cases;CtBackup::StagedRestore retry;
 result=CtBackup::StageValidated7zRestore(archive,root/L"retry",nullptr,nullptr,retry);Status(L"FRESH_RETRY_STAGE",result);
 Check(result.ok()&&retry.validation&&retry.profileCount==2&&SamePayload(sourceBefore,Snapshot(retry.payloadDir)),"A fresh sibling retry stages both exact client payloads after all stream simulations");
 const auto payloadBefore=Snapshot(retry.payloadDir);
 result=CtBackup::CommitStagedRestore(retry,live,recovery);Status(L"FRESH_RETRY_COMMIT",result);
 Check(result.ok()&&Snapshot(live)==payloadBefore&&Snapshot(recovery)==liveBefore,"Fresh retry commits exact staged object identities and retains the original live collection in recovery");
 Check(Snapshot(source)==sourceBefore&&Snapshot(archive)==archiveBefore&&Snapshot(shortRoot)==shortBefore&&Snapshot(foreignFile)==retainedForeign&&Snapshot(foreignRoot).entries.size()==2,"Retry preserves source/archive, independent successful stage and retained foreign data");
 Check(!restore_output_boundary::liveWrappers.load()&&_7zTest7z(archive.c_str(),nullptr,nullptr)==S_OK,"All composed stream owners close and the unchanged original archive remains CRC-valid");
 if(zeroOutcome=="RecoveredExact")Check(Snapshot(zeroLivePath)==zeroInstalled&&Snapshot(zeroRecoveryPath)==zeroPrevious,"Later stream failures and main retry leave the independent zero-result recovery commit exact");
 const auto bytes=PhysicalBytes(root);Check(bytes<=kBudget,"All source/archive/staging/live/recovery fixtures fit the explicit two MiB limit");
 std::cout<<"PHYSICAL_FIXTURE_BYTES="<<bytes<<"\nSTREAM_RESULT_SIMULATION_ONLY=1\nNATIVE_WRITEFILE_INJECTION=0\nDISK_EXHAUSTION_TESTED=0\nFLUSH_FAILURE_TESTED=0\nSHORT_WRITE_COMPLETED=1\nPARTIAL_DISKFULL_REJECTED=1\nZERO_CONTRACT_VIOLATION_SAFE=1\nZERO_CONTRACT_OUTCOME="<<zeroOutcome<<"\nCLOSE_RESULT_REJECTED=1\nFOREIGN_ADDITION_RETAINED=1\nFRESH_RETRY_COMMITTED=1\n";
}
}
int wmain(int argc,wchar_t**argv){
 if(argc!=4||wcscmp(argv[1],L"--headless"))return 2;
 const std::wstring_view mode=argv[3];
 if(mode!=L"--attribute-before"&&mode!=L"--attribute-after")return 2;
 try{
  const auto root=Reserve(argv[2]);
  RunAttributes(root/L"a",mode==L"--attribute-before");
  Run(root);
  std::cout<<"VERSION="<<CTSPACES_VERSION_TEXT<<"\nCASES="<<cases<<"\nCHECKS="<<checks
           <<"\nUNEXPECTED_FAILURES=0\nREGRESSION_PASSED=1\n";
  return 0;
 }catch(const std::exception&error){
  std::cerr<<"Restore output regression failed: "<<error.what()<<"\nCHECKS="<<checks<<"\nREGRESSION_PASSED=0\n";
  return 1;
 }
}
