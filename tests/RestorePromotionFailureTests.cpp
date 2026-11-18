// Ordinary valid-backup post-promotion verification failure/rollback ownership.
// No UI, malformed archives, external data, or recursive cleanup is linked.
#include "../BackupEngine.h"
#include "../version.h"
#include "RestorePromotionNativeBoundary.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#ifndef CTSPACES_RESTORE_PROMOTION_FIXTURE_PARENT
#error Bind this suite to its fresh workspace evidence directory through the wrapper.
#endif
#ifdef CTSPACES_ARCHIVE_ITEM_LIMIT_FOR_TEST
#error Ordinary production archive limits are required.
#endif
static_assert(CtArchiveSafety::kMaxItems==500000);
namespace fs=std::filesystem;
namespace {
constexpr unsigned long long kBudget=2ull*1024*1024;
unsigned checks=0,cases=0;
void Check(bool good,const char* message){++checks;std::cout<<(good?"PASS ":"FAIL ")<<message<<std::endl;if(!good)throw std::runtime_error(message);}
void NoReparse(const fs::path& path){for(auto current=path;!current.empty();){const DWORD attributes=GetFileAttributesW(current.c_str());if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Reparse fixture object/ancestor refused");const auto parent=current.parent_path();if(parent==current)break;current=parent;}}
fs::path Reserve(const wchar_t* text) {
 const fs::path requested(text), parent(CTSPACES_RESTORE_PROMOTION_FIXTURE_PARENT);
 const auto leaf = requested.filename().native();
 const bool roundLeaf = leaf.size() == 1 && ((leaf.front() >= L'0' && leaf.front() <= L'9') ||
                                            (leaf.front() >= L'a' && leaf.front() <= L'z'));
 if (!requested.is_absolute() || requested.lexically_normal() != requested ||
     requested.parent_path() != parent || !roundLeaf)
  throw std::runtime_error("Fresh compile-bound fixture required");
 NoReparse(requested);
 if (GetFileAttributesW(requested.c_str()) != INVALID_FILE_ATTRIBUTES ||
     !CreateDirectoryW(requested.c_str(), nullptr))
  throw std::runtime_error("Existing or unavailable fixture refused");
 NoReparse(requested);
 return requested;
}
void WriteNew(const fs::path& path,const std::string& bytes){if(bytes.size()>65536)throw std::runtime_error("Ordinary fixture file budget exceeded");NoReparse(path);fs::create_directories(path.parent_path());NoReparse(path);HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Fresh ordinary fixture write refused");DWORD wrote=0;const bool good=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&wrote,nullptr)&&wrote==bytes.size()&&FlushFileBuffers(file);CloseHandle(file);if(!good)throw std::runtime_error("Fixture write failed");}
std::string Read(const fs::path& path){if(fs::file_size(path)>kBudget)throw std::runtime_error("Oversized fixture refused");std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error("Private ordinary file read failed");return {std::istreambuf_iterator<char>(stream),{}};}
struct Identity {DWORD volume=0,high=0,low=0;bool operator==(const Identity&)const=default;};
Identity ObjectIdentity(HANDLE file){BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(file,&info)||GetFileType(file)!=FILE_TYPE_DISK||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Unsafe/unavailable private object identity");return {info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow};}
struct Entry {bool directory=false;Identity id;DWORD attributes=0;unsigned long long creation=0,write=0;std::string bytes;bool operator==(const Entry&)const=default;};
unsigned long long Time(FILETIME value){return (static_cast<unsigned long long>(value.dwHighDateTime)<<32)|value.dwLowDateTime;}
Entry Capture(const fs::path& path){NoReparse(path);HANDLE file=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Private inventory binding failed");BY_HANDLE_FILE_INFORMATION info{};const bool good=GetFileInformationByHandle(file,&info)&&GetFileType(file)==FILE_TYPE_DISK&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&((info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||info.nNumberOfLinks==1);CloseHandle(file);if(!good)throw std::runtime_error("Unsafe private inventory object");Entry entry;entry.directory=(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;entry.id={info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow};entry.attributes=info.dwFileAttributes;entry.creation=Time(info.ftCreationTime);entry.write=Time(info.ftLastWriteTime);if(!entry.directory)entry.bytes=Read(path);return entry;}
struct Tree {bool exists=false;std::map<std::wstring,Entry> entries;bool operator==(const Tree&)const=default;};
Tree Snapshot(const fs::path& root){NoReparse(root);const DWORD attributes=GetFileAttributesW(root.c_str());if(attributes==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)return {};throw std::runtime_error("Private inventory root unavailable");}Tree tree;tree.exists=true;tree.entries.emplace(L"",Capture(root));if(attributes&FILE_ATTRIBUTE_DIRECTORY)for(const auto& entry:fs::recursive_directory_iterator(root))tree.entries.emplace(entry.path().lexically_relative(root).native(),Capture(entry.path()));return tree;}
std::wstring Key(const fs::path& path){auto key=fs::absolute(path).lexically_normal().native();std::replace(key.begin(),key.end(),L'/',L'\\');return key;}
bool SamePath(const fs::path& a,const fs::path& b){return _wcsicmp(Key(a).c_str(),Key(b).c_str())==0;}
enum class Mode {Rollback,OccupiedStage,OccupiedLive};
struct Boundary {
 bool armed=false,promoted=false,hit=false,foreignCreated=false;
 Mode mode=Mode::Rollback;
 fs::path live,recovery,payload;
 Identity originalRoot,payloadRoot,payloadFile;
 unsigned originalMoves=0,promotions=0,payloadReturns=0,originalReturns=0,failedReads=0;
 Tree foreign;
} state;
void CreateForeign(const fs::path& path){NoReparse(path);if(!CreateDirectoryW(path.c_str(),nullptr))throw std::runtime_error("Foreign ordinary target must be vacant and fresh");WriteNew(path/L"Foreign/marker.txt","FOREIGN OWNER: KEEP THIS DATA");state.foreign=Snapshot(path);state.foreignCreated=true;}
void Status(const CtBackup::Result& result){std::wcout<<L"RESULT=0x"<<std::hex<<std::setw(8)<<std::setfill(L'0')<<static_cast<unsigned long>(result.code)<<std::dec<<L"\nDETAILS="<<result.details<<L'\n';}
bool RefuseRetry(void* user,std::wstring& detail){++*static_cast<unsigned*>(user);detail=L"Private old-seal validity probe refuses promotion";return false;}
void Run(const fs::path& root,Mode mode){
 ++cases;state={};state.mode=mode;
 const auto source=root/L"s/Sites",archive=root/L"o/b.7z",live=root/L"l/Sites",recovery=root/L"l/R";
 fs::create_directories(archive.parent_path());
 std::string payload(32768,'\0');unsigned random=0x6350A23u;for(auto& byte:payload){random^=random<<13;random^=random>>17;random^=random<<5;byte=static_cast<char>(random>>8);}
 WriteNew(source/L"Alpha/state.bin",payload);WriteNew(source/L"Beta/note.txt","A second client stays isolated through restore verification.");WriteNew(live/L"Keep/data.bin","ORIGINAL LIVE CLIENT DATA");
 Check(CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(live,L"Alpha"),"Ordinary target fits unchanged production client path limits");
 const auto original=Snapshot(live),sourceBefore=Snapshot(source);
 Check(CtBackup::CreateValidated7zBackup(source,archive,CTSPACES_VERSION_TEXT,nullptr,nullptr).ok(),"Create a complete ordinary validated backup without modifying source data");
 const auto archiveBefore=Capture(archive);
 Check(SUCCEEDED(_7zTest7z(archive.c_str(),nullptr,nullptr)),"Actual SDK CRC test confirms the unchanged ordinary archive before restore");
 CtBackup::StagedRestore staged;
 Check(CtBackup::StageValidated7zRestore(archive,root/L"stage",nullptr,nullptr,staged).ok(),"Actual extraction and sealing prepare a valid tiny two-client restore");
 const auto replacement=Snapshot(staged.payloadDir);
 state.live=live;state.recovery=recovery;state.payload=staged.payloadDir;
 state.originalRoot=original.entries.at(L"").id;state.payloadRoot=replacement.entries.at(L"").id;state.payloadFile=replacement.entries.at(L"Alpha\\state.bin").id;state.armed=true;
 const auto failed=CtBackup::CommitStagedRestore(staged,live,recovery);
 state.armed=false;Status(failed);
 Check(state.hit&&state.failedReads==1&&state.promotions==1&&state.originalMoves==1,"One installed-verification read fault occurs only after both real forward renames succeed");
 Check(failed.code==HRESULT_FROM_WIN32(ERROR_READ_FAULT),"Post-promotion failure preserves the exact native read HRESULT");
 Check(failed.details.find(L"A staged restore file changed or could not be read.")!=std::wstring::npos,"Returned diagnostic preserves the installed content-read failure");
 Check(Snapshot(source)==sourceBefore,"Failure and rollback preserve every source byte, timestamp and object identity");
 Check(Capture(archive)==archiveBefore,"Failure and rollback preserve exact archive bytes, timestamps and identity");
 Check(staged.validation&&staged.profileCount==2,"Failed promotion retains the existing token without silently replacing its seal");
 if(mode==Mode::Rollback){
  Check(state.payloadReturns==1&&state.originalReturns==1,"Both native rollback moves return the bound replacement and original objects");
  Check(Snapshot(live)==original,"Normal post-promotion rollback restores exact original live bytes and identities");
  Check(Snapshot(staged.payloadDir)==replacement,"Normal post-promotion rollback retains exact replacement bytes and identities in staging");
  Check(!fs::exists(recovery),"Successful rollback leaves no displaced original collection in recovery");
  unsigned guardCalls=0;
  const auto probe=CtBackup::CommitStagedRestore(staged,live,root/L"l/ProbeRecovery",RefuseRetry,&guardCalls);
  const bool stale=probe.code==HRESULT_FROM_WIN32(ERROR_FILE_INVALID)&&guardCalls==0;
  const bool stillSealed=probe.code==HRESULT_FROM_WIN32(ERROR_BUSY)&&guardCalls==1;
  Check(stale||stillSealed,"Old seal is either refused as stale or reaches an explicit refusing guard without bypassing validation");
  Check(Snapshot(live)==original&&Snapshot(staged.payloadDir)==replacement&&!fs::exists(root/L"l/ProbeRecovery"),"Old-seal validity probe leaves live and staged data unchanged");
  std::cout<<"OLD_SEAL_STALE="<<stale<<" GUARD_CALLS="<<guardCalls<<'\n';
  CtBackup::StagedRestore fresh;
  Check(CtBackup::StageValidated7zRestore(archive,root/L"fresh",nullptr,nullptr,fresh).ok(),"A fresh extraction and validation from the unchanged archive succeeds after rollback");
  const auto freshlyValidated=Snapshot(fresh.payloadDir);
  Check(CtBackup::CommitStagedRestore(fresh,live,root/L"l/R2").ok(),"Freshly staged retry commits without resealing or weakening the old token");
  Check(Snapshot(live)==freshlyValidated&&Snapshot(root/L"l/R2")==original,"Fresh retry installs exact new objects and preserves exact original live data in recovery");
  Check(Snapshot(staged.payloadDir)==replacement,"Fresh retry leaves the earlier retained replacement untouched");
 }else if(mode==Mode::OccupiedStage){
  Check(state.foreignCreated&&state.payloadReturns==0&&state.originalReturns==0,"Occupied staging return target prevents both unsafe rollback overwrites");
  Check(Snapshot(staged.payloadDir)==state.foreign,"Occupied staging target keeps exact foreign bytes, timestamps and identities");
  Check(Snapshot(live)==replacement&&Snapshot(recovery)==original,"Blocked staging rollback retains exact new live payload and exact old recovery data");
  Check(failed.details.find(L"installed payload was retained for recovery")!=std::wstring::npos&&failed.details.find(L"original Sites was retained in its recovery folder")!=std::wstring::npos,"Blocked rollback reports retention of both complete collections");
 }else{
  Check(state.foreignCreated&&state.payloadReturns==1&&state.originalReturns==0,"Foreign live target appears only after the bound replacement returns to staging");
  Check(Snapshot(live)==state.foreign,"Original return cannot overwrite a foreign live collection or its object identities");
  Check(Snapshot(staged.payloadDir)==replacement&&Snapshot(recovery)==original,"Occupied live return preserves exact replacement staging and exact original recovery");
  Check(failed.details.find(L"original Sites was retained in its recovery folder")!=std::wstring::npos,"Occupied original return reports retained old recovery data");
 }
 Check(Snapshot(source)==sourceBefore&&Capture(archive)==archiveBefore,"Final case leaves original archive and source data untouched");
 std::cout<<"CASE="<<static_cast<unsigned>(mode)<<" POST_PROMOTION_READ_FAULT=1 ORIGINAL_MOVES="<<state.originalMoves<<" PROMOTIONS="<<state.promotions<<" PAYLOAD_RETURNS="<<state.payloadReturns<<" ORIGINAL_RETURNS="<<state.originalReturns<<'\n';
}
}
namespace restore_promotion_boundary {
BOOL WINAPI ReadFile(HANDLE file,LPVOID data,DWORD bytes,LPDWORD read,LPOVERLAPPED overlapped){
 if(state.armed&&state.promoted&&!state.hit&&ObjectIdentity(file)==state.payloadFile){
  state.hit=true;++state.failedReads;
  if(state.mode==Mode::OccupiedStage)CreateForeign(state.payload);
  if(read)*read=0;SetLastError(ERROR_READ_FAULT);return FALSE;
 }
 return ::ReadFile(file,data,bytes,read,overlapped);
}
BOOL WINAPI SetFileInformationByHandle(HANDLE file,FILE_INFO_BY_HANDLE_CLASS kind,LPVOID information,DWORD bytes){
 if(!state.armed||kind!=FileRenameInfo)return ::SetFileInformationByHandle(file,kind,information,bytes);
 if(!information||bytes<offsetof(FILE_RENAME_INFO,FileName))throw std::runtime_error("Invalid native fixture rename buffer");
 const auto* rename=static_cast<const FILE_RENAME_INFO*>(information);
 if(rename->FileNameLength%sizeof(wchar_t)||rename->FileNameLength>bytes-offsetof(FILE_RENAME_INFO,FileName))throw std::runtime_error("Invalid native fixture rename name");
 const fs::path target(std::wstring(rename->FileName,rename->FileNameLength/sizeof(wchar_t)));
 const auto id=ObjectIdentity(file);
 const BOOL result=::SetFileInformationByHandle(file,kind,information,bytes);
 if(!result)return result;
 if(id==state.originalRoot&&SamePath(target,state.recovery))++state.originalMoves;
 if(id==state.payloadRoot&&SamePath(target,state.live)){++state.promotions;state.promoted=true;}
 if(id==state.payloadRoot&&SamePath(target,state.payload)){
  ++state.payloadReturns;state.promoted=false;
  if(state.mode==Mode::OccupiedLive&&state.hit)CreateForeign(state.live);
 }
 if(id==state.originalRoot&&SamePath(target,state.live))++state.originalReturns;
 return result;
}
}
int wmain(int argc,wchar_t** argv){try{
 std::cout<<std::unitbuf;if(argc!=3||wcscmp(argv[1],L"--headless"))throw std::runtime_error("Explicit headless fresh fixture required");
 const auto root=Reserve(argv[2]);Run(root/L"0",Mode::Rollback);Run(root/L"1",Mode::OccupiedStage);Run(root/L"2",Mode::OccupiedLive);
 unsigned long long bytes=0;for(const auto& [name,entry]:Snapshot(root).entries){(void)name;if(entry.bytes.size()>kBudget-bytes)throw std::runtime_error("Two MiB total fixture budget exceeded");bytes+=entry.bytes.size();}
 Check(bytes<=kBudget,"All retained ordinary physical fixture payloads remain within two MiB");
 std::cout<<"VERSION="<<CTSPACES_VERSION_TEXT<<"\nORDINARY_VALID_ARCHIVES=1\nPHYSICAL_FIXTURE_BYTES="<<bytes<<"\nFIXTURE_BUDGET_BYTES="<<kBudget<<"\nCASES="<<cases<<"\nCHECKS="<<checks<<"\nREGRESSION_PASSED=1\nUNEXPECTED_FAILURES=0\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<"\nREGRESSION_PASSED=0\n";return 1;}}
