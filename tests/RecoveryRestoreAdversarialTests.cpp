// Ordinary valid SDK archives and the actual app recovery allocator/backend.
// Only two precisely identified native rename calls acquire competing fixture
// destinations. No malformed archive, real user data, or visible UI is used.
#include <windows.h>
#include <filesystem>
#include <cstddef>
#include "RecoveryRestoreNativeBoundary.h"
#define wmain UnusedRecoveryRestoreArchiveLifetimeEntry
#include "ArchiveUiLifetimeTests.cpp"
#undef wmain

namespace recovery_restore_boundary {
struct Identity { DWORD volume=0,high=0,low=0;bool operator==(const Identity&)const=default; };
enum class Mode { None, RecoveryCollision, ForeignLiveAfterMove };
struct State {
 Mode mode=Mode::None;bool armed=false,hit=false,guardRan=false,noReplace=true;
 unsigned matchingCalls=0,originalMoves=0,promotions=0,originalReturns=0,failedNative=0;
 DWORD nativeError=ERROR_SUCCESS;
 fs::path live,recovery,payload;Identity original,payloadIdentity;PrivateTree foreign;
} state;
static Identity HandleIdentity(HANDLE object){BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(object,&info)||GetFileType(object)!=FILE_TYPE_DISK||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Unsafe bound rename source in private fixture");return {info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow};}
static Identity PathIdentity(const fs::path& path){const auto entry=PrivateObject(path);return {entry.volume,entry.high,entry.low};}
static bool SamePath(const fs::path& left,const fs::path& right){return left.lexically_normal().native()==right.lexically_normal().native();}
static void CreateForeign(const fs::path& path){
 if(!CreateDirectoryW(path.c_str(),nullptr))throw std::runtime_error("Competing fixture destination was not absent");
 const auto marker=path/L"foreign-owner.txt";HANDLE file=CreateFileW(marker.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Competing fixture marker creation failed");
 constexpr char bytes[]="FOREIGN OWNER: never overwrite or delete";DWORD written=0;const bool good=WriteFile(file,bytes,sizeof(bytes)-1,&written,nullptr)&&written==sizeof(bytes)-1&&FlushFileBuffers(file);CloseHandle(file);if(!good)throw std::runtime_error("Competing fixture write failed");state.foreign=PrivateSnapshot(path);
}
BOOL WINAPI SetFileInformationByHandle(HANDLE object,FILE_INFO_BY_HANDLE_CLASS kind,LPVOID information,DWORD bytes){
 if(!state.armed||kind!=FileRenameInfo)return ::SetFileInformationByHandle(object,kind,information,bytes);
 if(!information||bytes<offsetof(FILE_RENAME_INFO,FileName))throw std::runtime_error("Invalid private native rename buffer");
 const auto* rename=static_cast<const FILE_RENAME_INFO*>(information);
 if(rename->FileNameLength%sizeof(wchar_t)||rename->FileNameLength>bytes-(offsetof(FILE_RENAME_INFO, FileName)))throw std::runtime_error("Invalid private native rename target");
 const fs::path target(std::wstring(rename->FileName,rename->FileNameLength/sizeof(wchar_t)));const auto identity=HandleIdentity(object);
 const bool originalForward=identity==state.original&&SamePath(target,state.recovery);
 const bool payloadForward=identity==state.payloadIdentity&&SamePath(target,state.live);
 const bool originalReturn=identity==state.original&&SamePath(target,state.live);
 if(!originalForward&&!payloadForward&&!originalReturn)return ::SetFileInformationByHandle(object,kind,information,bytes);
 ++state.matchingCalls;state.noReplace=state.noReplace&&!rename->ReplaceIfExists&&rename->RootDirectory==nullptr;
 const bool inject=!state.hit&&((state.mode==Mode::RecoveryCollision&&originalForward)||(state.mode==Mode::ForeignLiveAfterMove&&payloadForward&&state.originalMoves==1));
 if(inject){if(!state.guardRan)throw std::runtime_error("The final commit guard did not run before the native collision boundary");CreateForeign(target);state.hit=true;}
 const BOOL result=::SetFileInformationByHandle(object,kind,information,bytes);const DWORD error=result?ERROR_SUCCESS:GetLastError();
 if(result){if(originalForward)++state.originalMoves;if(payloadForward)++state.promotions;if(originalReturn)++state.originalReturns;}
 else if(inject){++state.failedNative;state.nativeError=error;}
 SetLastError(error);return result;
}
}

static unsigned adversarialCases=0;
static unsigned guardCases=0;
static bool unicodeSequential=false,missingSites=false,lastInstantRecovery=false,foreignLiveRetained=false,freshRetry=false;
static void Require(bool value,const char* message){Check(value,message);if(!value)throw std::runtime_error(message);}
static void WriteNew(const fs::path& path,const std::string& bytes){
 fs::create_directories(path.parent_path());HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Fresh private file target required");DWORD wrote=0;const bool good=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&wrote,nullptr)&&wrote==bytes.size()&&FlushFileBuffers(file);CloseHandle(file);if(!good)throw std::runtime_error("Private fixture write failed");
}
static fs::path Allocate(const fs::path& parent,const std::wstring& timestamp){DWORD error=ERROR_GEN_FAILURE;const auto path=UniqueRecoveryPath(parent,timestamp,&error);Require(!path.empty()&&error==ERROR_SUCCESS&&path.parent_path()==parent&&path.filename().native().size()==5,"Actual application allocator returns a compact unoccupied sibling in the bound parent");return path;}
static CtBackup::StagedRestore Stage(const fs::path& archive,const fs::path& root){CtBackup::StagedRestore staged;const auto result=CtBackup::StageValidated7zRestore(archive,root,nullptr,nullptr,staged);Require(result.ok()&&staged.validation,"Actual SDK extraction validates and seals an ordinary backup");return staged;}
static bool AllowCommit(void* user,std::wstring&){++*static_cast<unsigned*>(user);if(recovery_restore_boundary::state.armed)recovery_restore_boundary::state.guardRan=true;return true;}
static void ExactArchive(const fs::path& source,const PrivateTree& sourceBefore,const fs::path& archive,const PrivateEntry& archiveBefore){Check(PrivateSnapshot(source)==sourceBefore&&PrivateObject(archive)==archiveBefore&&_7zTest7z(archive.c_str(),nullptr,nullptr)==S_OK,"Every source object and original valid archive retain their exact bytes and identities");}
static void Arm(recovery_restore_boundary::Mode mode,const fs::path& live,const fs::path& recovery,const CtBackup::StagedRestore& staged){using namespace recovery_restore_boundary;state={};state.mode=mode;state.live=live;state.recovery=recovery;state.payload=staged.payloadDir;state.original=PathIdentity(live);state.payloadIdentity=PathIdentity(staged.payloadDir);state.armed=true;}

static void UnicodeRepeatedRestore(const fs::path& root,const fs::path& source,const fs::path& archive,const std::wstring& alpha,const std::wstring& beta){
 ++adversarialCases;const auto parent=root/L"U-\u00C9-\u65E5-\U0001F680",live=parent/L"Sites";const std::wstring stamp=L"2026-10-31_071822";
 WriteNew(live/alpha/L"ctSpaces-client-notes.ctn","CURRENT ALPHA ORIGINAL");WriteNew(live/beta/L"ctSpaces-client-notes.ctn","EXCLUDED BETA ORIGINAL");WriteNew(live/beta/L"edge/Default/state.bin","private original Beta browser state");
 const auto legacy=parent/(L"Sites_PreRestore_"+stamp);WriteNew(legacy/L"sentinel","earlier legacy recovery retained");const auto legacyBefore=PrivateSnapshot(legacy),original=PrivateSnapshot(live);const auto sourceBefore=PrivateSnapshot(source);const auto archiveBefore=PrivateObject(archive);
 Require(CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(live,alpha),"Unicode fixture fits the actual final-client reserve and Windows path budget");
 const auto recovery1=Allocate(parent,stamp);auto first=Stage(archive,root/L"s1");const auto payload1=PrivateSnapshot(first.payloadDir);
 Check(fs::exists(first.payloadDir/alpha/L"ctSpaces-client-notes.ctn")&&!fs::exists(first.payloadDir/beta),"Selected-client backup contains the Unicode Alpha notebook and excludes Beta entirely");
 unsigned guards=0;bool previousInRecovery=false;const auto firstResult=CtBackup::CommitStagedRestore(first,live,recovery1,AllowCommit,&guards,&previousInRecovery);
 Require(firstResult.ok(),"First actual restore succeeds under a Unicode parent with the compact recovery name");
 Check(guards==1&&previousInRecovery&&PrivateSnapshot(recovery1)==original&&PrivateSnapshot(live)==payload1&&!fs::exists(first.stagingRoot),"First restore reports and preserves the exact original collection and installs the exact sealed payload");
 Check(PrivateBytes(recovery1/beta/L"ctSpaces-client-notes.ctn")=="EXCLUDED BETA ORIGINAL"&&!fs::exists(live/beta),"Excluded client's original notebook stays in its recovery generation without mixing into restored Alpha");
 WriteNew(live/beta/L"ctSpaces-client-notes.ctn","EXCLUDED BETA ADDED AFTER FIRST RESTORE");const auto secondOriginal=PrivateSnapshot(live),firstRecovery=PrivateSnapshot(recovery1);
 const auto recovery2=Allocate(parent,stamp);auto second=Stage(archive,root/L"s2");const auto payload2=PrivateSnapshot(second.payloadDir);guards=0;
 Require(recovery2!=recovery1,"The identical restore timestamp selects a different compact sibling while the first is occupied");
 previousInRecovery=false;const auto secondResult=CtBackup::CommitStagedRestore(second,live,recovery2,AllowCommit,&guards,&previousInRecovery);
 Require(secondResult.ok(),"Second actual restore with the same timestamp succeeds without reusing the first recovery");
 unicodeSequential=guards==1&&previousInRecovery&&PrivateSnapshot(recovery1)==firstRecovery&&PrivateSnapshot(recovery2)==secondOriginal&&PrivateSnapshot(live)==payload2&&!fs::exists(second.stagingRoot);
 Check(unicodeSequential,"Both recovery generations and the installed payload retain their exact intended bytes and object identities");
 Check(PrivateBytes(recovery1/beta/L"ctSpaces-client-notes.ctn")=="EXCLUDED BETA ORIGINAL"&&PrivateBytes(recovery2/beta/L"ctSpaces-client-notes.ctn")=="EXCLUDED BETA ADDED AFTER FIRST RESTORE"&&!fs::exists(live/beta),"Separate excluded-client notebook generations remain separately recoverable and are never reassigned to another client");
 Check(PrivateSnapshot(legacy)==legacyBefore,"Both compact restores leave the legacy recovery collection unchanged");ExactArchive(source,sourceBefore,archive,archiveBefore);Audit();
}
static void MissingSitesRestore(const fs::path& root,const fs::path& source,const fs::path& archive){
 ++adversarialCases;const auto parent=root/L"N",live=parent/L"Sites";fs::create_directories(parent);const auto sourceBefore=PrivateSnapshot(source);const auto archiveBefore=PrivateObject(archive);const auto recovery=Allocate(parent,L"no-Sites");auto staged=Stage(archive,root/L"ns");const auto payload=PrivateSnapshot(staged.payloadDir);
 Require(!fs::exists(live)&&!fs::exists(recovery),"A new destination has neither Sites nor a precreated recovery reservation");unsigned guards=0;bool previousInRecovery=true;const auto result=CtBackup::CommitStagedRestore(staged,live,recovery,AllowCommit,&guards,&previousInRecovery);
 missingSites=result.ok()&&guards==1&&!previousInRecovery&&PrivateSnapshot(live)==payload&&!fs::exists(recovery)&&!fs::exists(staged.stagingRoot);
 Check(missingSites,"Actual restore into missing Sites installs exact objects without creating or reporting an old-data recovery");ExactArchive(source,sourceBefore,archive,archiveBefore);Audit();
}
struct GuardCollision { fs::path destination;unsigned calls=0;PrivateTree foreign; };
static bool OccupyFromGuard(void* user,std::wstring&){auto& collision=*static_cast<GuardCollision*>(user);++collision.calls;if(!CreateDirectoryW(collision.destination.c_str(),nullptr))throw std::runtime_error("Guard fixture destination was not vacant");WriteNew(collision.destination/L"guard-owner.txt","PRECOMMIT GUARD FOREIGN OWNER");collision.foreign=PrivateSnapshot(collision.destination);return true;}
static void PrecommitGuardCollisions(const fs::path& root,const fs::path& source,const fs::path& archive){
 for(unsigned scenario=0;scenario!=3;++scenario){
  ++adversarialCases;++guardCases;const auto parent=root/(L"G"+std::to_wstring(scenario)),live=parent/L"Sites";fs::create_directories(parent);const bool initiallyExists=scenario==2;
  if(initiallyExists)WriteNew(live/L"Existing/ctSpaces-client-notes.ctn","GUARD ORIGINAL CURRENT CLIENT");const auto original=PrivateSnapshot(live),sourceBefore=PrivateSnapshot(source);const auto archiveBefore=PrivateObject(archive);const auto recovery=Allocate(parent,L"precommit-guard");auto staged=Stage(archive,root/(L"gs"+std::to_wstring(scenario)));const auto stageBefore=PrivateSnapshot(staged.stagingRoot);GuardCollision collision{scenario==0?live:recovery};
  bool previousInRecovery=true;const auto result=CtBackup::CommitStagedRestore(staged,live,recovery,OccupyFromGuard,&collision,&previousInRecovery);
  Check(!result.ok()&&result.code==HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)&&collision.calls==1&&!previousInRecovery,"Actual precommit collision is refused before any move and does not report a retained original");
  const bool exactLive=scenario==0?PrivateSnapshot(live)==collision.foreign:PrivateSnapshot(live)==original;const bool exactRecovery=scenario==0?!fs::exists(recovery):PrivateSnapshot(recovery)==collision.foreign;
  Check(exactLive&&exactRecovery&&PrivateSnapshot(staged.stagingRoot)==stageBefore,"Guard-time collision preserves exact current/missing Sites, competing destination and sealed stage bytes and identities");ExactArchive(source,sourceBefore,archive,archiveBefore);Audit();
 }
}
static void LastInstantRecoveryCollision(const fs::path& root,const fs::path& source,const fs::path& archive){
 ++adversarialCases;const auto parent=root/L"C",live=parent/L"Sites";WriteNew(live/L"Existing/ctSpaces-client-notes.ctn","LAST-INSTANT ORIGINAL");const auto original=PrivateSnapshot(live),sourceBefore=PrivateSnapshot(source);const auto archiveBefore=PrivateObject(archive);const auto recovery=Allocate(parent,L"collision");auto staged=Stage(archive,root/L"cs");const auto stageBefore=PrivateSnapshot(staged.stagingRoot);
 Arm(recovery_restore_boundary::Mode::RecoveryCollision,live,recovery,staged);unsigned guards=0;bool previousInRecovery=true;const auto result=CtBackup::CommitStagedRestore(staged,live,recovery,AllowCommit,&guards,&previousInRecovery);recovery_restore_boundary::state.armed=false;const auto& observed=recovery_restore_boundary::state;
 lastInstantRecovery=!result.ok()&&guards==1&&!previousInRecovery&&observed.hit&&observed.failedNative==1&&observed.matchingCalls==1&&observed.originalMoves==0&&observed.promotions==0&&observed.noReplace&&observed.nativeError!=ERROR_SUCCESS;
 Check(lastInstantRecovery,"A competing recovery appears after the final guard/missing check and the actual non-replacing native rename refuses it");
 Check(PrivateSnapshot(live)==original&&PrivateSnapshot(staged.stagingRoot)==stageBefore&&PrivateSnapshot(recovery)==observed.foreign,"Last-instant collision preserves exact live, sealed stage and competing owner bytes and identities");
 const auto freshRecovery=Allocate(parent,L"collision");auto fresh=Stage(archive,root/L"cr");const auto freshPayload=PrivateSnapshot(fresh.payloadDir);guards=0;const auto retry=CtBackup::CommitStagedRestore(fresh,live,freshRecovery,AllowCommit,&guards,&previousInRecovery);
 Check(retry.ok()&&guards==1&&previousInRecovery&&freshRecovery!=recovery&&PrivateSnapshot(live)==freshPayload&&PrivateSnapshot(freshRecovery)==original,"A freshly validated retry skips the competing recovery and installs its own exact payload");
 Check(PrivateSnapshot(recovery)==observed.foreign&&PrivateSnapshot(staged.stagingRoot)==stageBefore,"Fresh retry retains the competing owner and earlier failed sealed stage without deleting or reusing them");ExactArchive(source,sourceBefore,archive,archiveBefore);Audit();
}
static void ForeignLiveAfterMove(const fs::path& root,const fs::path& source,const fs::path& archive){
 ++adversarialCases;const auto parent=root/L"F",live=parent/L"Sites";WriteNew(live/L"Existing/ctSpaces-client-notes.ctn","ORIGINAL BEFORE NATIVE PROMOTION COLLISION");const auto original=PrivateSnapshot(live),sourceBefore=PrivateSnapshot(source);const auto archiveBefore=PrivateObject(archive);const auto recovery=Allocate(parent,L"foreign-live");auto staged=Stage(archive,root/L"fs");const auto stageBefore=PrivateSnapshot(staged.stagingRoot);
 Arm(recovery_restore_boundary::Mode::ForeignLiveAfterMove,live,recovery,staged);unsigned guards=0;bool previousInRecovery=false;const auto result=CtBackup::CommitStagedRestore(staged,live,recovery,AllowCommit,&guards,&previousInRecovery);recovery_restore_boundary::state.armed=false;const auto& observed=recovery_restore_boundary::state;
 foreignLiveRetained=!result.ok()&&guards==1&&observed.hit&&observed.failedNative==1&&observed.originalMoves==1&&observed.promotions==0&&observed.originalReturns==0&&observed.noReplace;
 Check(foreignLiveRetained,"Actual original move succeeds, then a new live owner blocks payload promotion and automatic original return without replacement");
 Check(PrivateSnapshot(recovery)==original&&PrivateSnapshot(staged.stagingRoot)==stageBefore&&PrivateSnapshot(live)==observed.foreign,"Occupied rollback destination retains exact original recovery, exact unpromoted sealed stage and exact foreign live collection");
 Check(previousInRecovery&&result.details.find(L"original Sites was retained in its recovery folder")!=std::wstring::npos,"Failed rollback reports that the original collection remains in its recovery folder");
 // Deliberate fixture-only conflict preservation and a NEW stage; this does not
 // model a blind automatic retry of a partially completed production restore.
 const auto preserved=parent/L"preserved-foreign";fs::rename(live,preserved);Check(PrivateSnapshot(preserved)==observed.foreign&&!fs::exists(live),"Explicit conflict handling preserves the foreign owner in another absent private path");
 auto fresh=Stage(archive,root/L"fr");const auto payload=PrivateSnapshot(fresh.payloadDir);const auto retryRecovery=Allocate(parent,L"foreign-live");guards=0;const auto retry=CtBackup::CommitStagedRestore(fresh,live,retryRecovery,AllowCommit,&guards,&previousInRecovery);
 freshRetry=retry.ok()&&guards==1&&!previousInRecovery&&PrivateSnapshot(live)==payload&&!fs::exists(retryRecovery)&&!fs::exists(fresh.stagingRoot);
 Check(freshRetry,"Only after explicit conflict preservation a freshly staged restore safely fills the now-missing live destination");
 Check(PrivateSnapshot(recovery)==original&&PrivateSnapshot(staged.stagingRoot)==stageBefore&&PrivateSnapshot(preserved)==observed.foreign,"Fresh success keeps the exact original recovery, failed stage and foreign owner independently recoverable");ExactArchive(source,sourceBefore,archive,archiveBefore);Audit();
}
int wmain(int argc,wchar_t**argv){
 HHOOK cbt=nullptr;std::vector<HWINEVENTHOOK> hooks;
 try{
  std::cout<<std::unitbuf;if(argc!=3||wcscmp(argv[1],L"--headless"))throw std::runtime_error("Explicit headless bound fixture required");const fs::path parent(CTSPACES_RECOVERY_ADVERSARIAL_FIXTURE_PARENT),requested(argv[2]);
  if(!parent.is_absolute()||!requested.is_absolute()||requested.parent_path()!=parent||requested.filename()!=L"f")throw std::runtime_error("Only the compile-bound fresh private child is allowed");
  for(auto path=requested;!path.empty();){const auto attributes=GetFileAttributesW(path.c_str());if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Fixture reparse ancestor refused");const auto next=path.parent_path();if(next==path)break;path=next;}
  if(fs::exists(requested)||!fs::create_directory(requested))throw std::runtime_error("Fresh fixture required; existing data retained");fixture=requested;
  cbt=SetWindowsHookExW(WH_CBT,BackgroundCbt,nullptr,GetCurrentThreadId());if(!cbt)throw std::runtime_error("Native activation veto unavailable");
  for(DWORD event:{EVENT_OBJECT_SHOW,EVENT_OBJECT_FOCUS,EVENT_SYSTEM_FOREGROUND}){const auto hook=SetWinEventHook(event,event,nullptr,Event,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);if(!hook)throw std::runtime_error("Native observer unavailable");hooks.push_back(hook);}
  g_hInst=GetModuleHandleW(nullptr);g_bQaInstance=true;g_sExeDir=GetCurrentExecutablePath().parent_path();ownerThread=GetCurrentThreadId();WNDCLASSW cls{};cls.lpfnWndProc=HiddenProc;cls.hInstance=g_hInst;cls.lpszClassName=L"RecoveryRestorePrivateHidden";if(!RegisterClassW(&cls))throw std::runtime_error("Private hidden class unavailable");g_hGui=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"Private restore adversarial proof",WS_OVERLAPPED,0,0,600,240,nullptr,nullptr,g_hInst,nullptr);if(!g_hGui)throw std::runtime_error("Private hidden owner unavailable");
  const std::wstring alpha=L"A-\u00C9-\u65E5",beta=L"B-\u0411-\u672C";const auto source=fixture/L"s/Sites",archive=fixture/L"a/ordinary.7z";WriteNew(source/alpha/L"state.bin",std::string(8192,'A'));WriteNew(source/alpha/L"ctSpaces-client-notes.ctn","SELECTED UNICODE ALPHA NOTEBOOK");WriteNew(source/beta/L"ctSpaces-client-notes.ctn","EXCLUDED BACKUP-SOURCE BETA NOTEBOOK");
  fs::create_directories(archive.parent_path());CtBackup::BackupOptions options;options.selectedClientNames=std::vector<std::wstring>{alpha};const auto sourceBefore=PrivateSnapshot(source);const auto created=CtBackup::CreateValidated7zBackup(source,archive,CTSPACES_VERSION_TEXT,nullptr,nullptr,options);Require(created.ok()&&_7zTest7z(archive.c_str(),nullptr,nullptr)==S_OK,"A production selected-client backup is ordinary, CRC-valid and leaves its source intact");Check(PrivateSnapshot(source)==sourceBefore,"Backup selection preserves exact included and excluded source client bytes and identities");
  UnicodeRepeatedRestore(fixture,source,archive,alpha,beta);MissingSitesRestore(fixture,source,archive);PrecommitGuardCollisions(fixture,source,archive);LastInstantRecoveryCollision(fixture,source,archive);ForeignLiveAfterMove(fixture,source,archive);
  unsigned long long bytes=0;for(const auto& object:fs::recursive_directory_iterator(fixture))if(object.is_regular_file())bytes+=object.file_size();Check(bytes<=2*1024*1024,"All retained ordinary physical fixtures stay within the explicit two MiB budget");Audit();Check(!forbidden&&!nativeShows&&!nativeFocus&&!nativeForeground,"The adversarial proof uses no real show, focus, foreground, clipboard, browser or device input");
  DestroyWindow(g_hGui);g_hGui=nullptr;for(auto hook:hooks)UnhookWinEvent(hook);hooks.clear();UnhookWindowsHookEx(cbt);cbt=nullptr;
  std::cout<<"VERSION="<<CTSPACES_VERSION_TEXT<<"\nCHECKS="<<checks<<"\nCASES="<<adversarialCases<<"\nGUARD_CASES="<<guardCases<<"\nFAILURES="<<failures<<"\nPHYSICAL_FIXTURE_BYTES="<<bytes<<"\nNATIVE_EVENTS="<<nativeShows<<'/'<<nativeFocus<<'/'<<nativeForeground<<"\nFORBIDDEN="<<forbidden<<"\nORDINARY_VALID_ARCHIVE=1\nUNICODE_SEQUENTIAL="<<unicodeSequential<<"\nMISSING_SITES="<<missingSites<<"\nLAST_INSTANT_RECOVERY_COLLISION="<<lastInstantRecovery<<"\nFOREIGN_LIVE_RETAINED="<<foreignLiveRetained<<"\nFRESH_RETRY="<<freshRetry<<"\nNATIVE_RENAME_BOUNDARY_ONLY=1\n";return failures?1:0;
 }catch(const std::exception& error){recovery_restore_boundary::state.armed=false;std::cerr<<"Private restore adversarial error: "<<error.what()<<'\n';if(g_hGui)DestroyWindow(g_hGui);for(auto hook:hooks)UnhookWinEvent(hook);if(cbt)UnhookWindowsHookEx(cbt);return 2;}
}
