// Actual application recovery-name allocation plus ordinary SDK restore data.
// Reuse the existing app-inclusion safety boundary, not its entry point/cases.
#include <windows.h>
#include <filesystem>
#include <bitset>
#include <type_traits>
namespace recovery_probe {
enum class Mode { Real, Occupied, Denied, MissingParent, ReparseFirst };
static Mode mode=Mode::Real;
static std::filesystem::path parent;
static unsigned calls=0,duplicates=0,unexpected=0;
static std::bitset<65536> seen;
static DWORD WINAPI Attributes(LPCWSTR);
}
#define GetFileAttributesW recovery_probe::Attributes
#define wmain UnusedRecoveryArchiveLifetimeEntry
#include "ArchiveUiLifetimeTests.cpp"
#undef wmain
#undef GetFileAttributesW

namespace recovery_probe {
static DWORD WINAPI Attributes(LPCWSTR name){
 const std::filesystem::path path(name?name:L"");
 if(mode==Mode::Real||path.parent_path()!=parent)return ::GetFileAttributesW(name);
 const auto leaf=path.filename().native();
 if(leaf.size()!=5||leaf[0]!=L'R'){++unexpected;return ::GetFileAttributesW(name);}
 unsigned value=0;
 for(size_t i=1;i<leaf.size();++i){const auto c=leaf[i];if(!((c>=L'0'&&c<=L'9')||(c>=L'A'&&c<=L'F'))){++unexpected;return ::GetFileAttributesW(name);}value=(value<<4)|(c<=L'9'?c-L'0':c-L'A'+10);}
 ++calls;if(seen.test(value))++duplicates;seen.set(value);
 if(mode==Mode::Occupied)return FILE_ATTRIBUTE_NORMAL;
 if(mode==Mode::Denied){SetLastError(ERROR_ACCESS_DENIED);return INVALID_FILE_ATTRIBUTES;}
 if(mode==Mode::MissingParent){SetLastError(ERROR_PATH_NOT_FOUND);return INVALID_FILE_ATTRIBUTES;}
 if(mode==Mode::ReparseFirst&&calls==1)return FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT;
 return ::GetFileAttributesW(name);
}
static void Begin(Mode next,const fs::path& root){mode=next;parent=root;calls=duplicates=unexpected=0;seen.reset();}
static void End(){mode=Mode::Real;}
}
template<class Allocate>
static fs::path AllocateRecovery(Allocate allocator,const fs::path& parent,const std::wstring& stamp,DWORD& error){
 if constexpr(std::is_invocable_v<Allocate,const fs::path&,const std::wstring&,DWORD*>)return allocator(parent,stamp,&error);
 else{error=ERROR_SUCCESS;return allocator(parent,stamp);}
}
static fs::path Recovery(const fs::path& parent,const std::wstring& stamp,DWORD& error){return AllocateRecovery(&UniqueRecoveryPath,parent,stamp,error);}
static void RecoveryWrite(const fs::path& path,const std::string& bytes){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));out.flush();if(!out)throw std::runtime_error("Private recovery fixture write failed");}
static unsigned recoveryCases=0;
static bool longCommit=false,callerBeforeStage=false;
static void AllocatorCases(const fs::path& root){
 ++recoveryCases;const auto parent=root/L"allocate";fs::create_directories(parent);const std::wstring stamp=L"2026-10-31_013731";
 const auto legacy=parent/(L"Sites_PreRestore_"+stamp);RecoveryWrite(legacy/L"old.txt","legacy recovery retained");const auto legacyBefore=PrivateSnapshot(legacy);
 DWORD error=ERROR_INVALID_FUNCTION;const auto first=Recovery(parent,stamp,error);
 Check(!first.empty()&&first.parent_path()==parent&&first.filename().native().size()<=5&&error==ERROR_SUCCESS,"Recovery is a compact same-parent sibling without growing the Sites path budget");
 RecoveryWrite(first/L"sentinel","first allocated recovery");const auto firstBefore=PrivateSnapshot(first);
 const auto second=Recovery(parent,stamp,error);Check(!second.empty()&&second!=first&&second.parent_path()==parent,"A repeated timestamp skips an occupied directory without reuse");
 RecoveryWrite(second,"occupied ordinary file");const auto secondBefore=PrivateObject(second);
 const auto third=Recovery(parent,stamp,error);Check(!third.empty()&&third!=first&&third!=second&&third.filename().native().size()<=5,"The same allocator skips occupied files without lengthening its name");
 Check(PrivateSnapshot(first)==firstBefore&&PrivateObject(second)==secondBefore&&PrivateSnapshot(legacy)==legacyBefore,"Allocation preserves existing directory, file and legacy recovery bytes and identities");
 Check(PrivateRecoveryName(legacy)&&PrivateRecoveryName(parent/L"R00AF")&&!PrivateRecoveryName(parent/L"Rcache"),"Recovery discovery accepts legacy and compact names without accepting unrelated folders");
 const auto before=PrivateSnapshot(parent);
 ++recoveryCases;recovery_probe::Begin(recovery_probe::Mode::ReparseFirst,parent);const auto afterReparse=Recovery(parent,L"reparse",error);recovery_probe::End();
 Check(!afterReparse.empty()&&recovery_probe::calls==2&&recovery_probe::duplicates==0&&recovery_probe::unexpected==0,"A reparse-shaped occupied candidate is skipped at the actual attribute API boundary");
 ++recoveryCases;recovery_probe::Begin(recovery_probe::Mode::Occupied,parent);const auto exhausted=Recovery(parent,L"occupied",error);recovery_probe::End();
 Check(exhausted.empty()&&error==ERROR_ALREADY_EXISTS,"A fully occupied recovery namespace is refused with its real exhaustion error");
 Check(recovery_probe::calls==65536&&recovery_probe::seen.all()&&recovery_probe::duplicates==0&&recovery_probe::unexpected==0,"Bounded probing visits every compact slot exactly once including wrap without creating sixty-five thousand files");
 ++recoveryCases;recovery_probe::Begin(recovery_probe::Mode::Denied,parent);const auto denied=Recovery(parent,L"denied",error);recovery_probe::End();
 Check(denied.empty()&&error==ERROR_ACCESS_DENIED&&recovery_probe::calls==1,"A native attribute access error fails closed without returning an unverified destination");
 ++recoveryCases;recovery_probe::Begin(recovery_probe::Mode::MissingParent,parent);const auto missing=Recovery(parent,L"missing",error);recovery_probe::End();
 Check(missing.empty()&&error==ERROR_PATH_NOT_FOUND&&recovery_probe::calls==1,"A missing-parent probe is refused rather than mistaken for a free child");
 Check(PrivateSnapshot(parent)==before,"API-only collision and error probes make no filesystem changes");Audit();
}
static void BoundaryAndTransactions(const fs::path& root){
 const auto source=root/L"source/Sites",archive=root/L"ordinary.7z";
 const std::string newBytes(8192,'N');RecoveryWrite(source/L"Restored/state.bin",newBytes);RecoveryWrite(source/L"Restored/ctSpaces-client-notes.ctn","ordinary retained notebook fixture bytes");
 const auto sourceBefore=PrivateSnapshot(source);const auto backed=CtBackup::CreateValidated7zBackup(source,archive,CTSPACES_VERSION_TEXT,nullptr,nullptr);
 Check(backed.ok()&&_7zTest7z(archive.c_str(),nullptr,nullptr)==S_OK,"The focused restore uses an ordinary CRC-valid production backup");if(!backed.ok())throw std::runtime_error("Ordinary recovery backup unavailable");const auto archiveBefore=PrivateObject(archive);
 ++recoveryCases;const auto base=root/L"boundary";fs::create_directories(base);if(base.native().size()>=89)throw std::runtime_error("Use a shorter private fixture prefix for the real recovery-budget proof");const auto data=base/std::wstring(90-base.native().size()-1,L'd');const auto live=data/L"Sites";const auto client=live/L"Existing";
 const auto deep=client/std::wstring(247-client.native().size()-1,L'p');const auto longest=deep/L"stateAB.bin";const std::string oldBytes(8192,'O');RecoveryWrite(longest,oldBytes);RecoveryWrite(client/L"ctSpaces-client-notes.ctn","original notebook retained exactly");
 Check(deep.native().size()==247&&longest.native().size()==259&&CtBackup::Detail::IsTreeTargetWithinLegacyPathBudget(live,live),"Existing ordinary client data fits the unchanged exact directory247/file259 Windows budget");
 Check(CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(live,L"Restored"),"The replacement client fits the separate unchanged final-client reserve before recovery mapping is tested");
 const auto old=PrivateSnapshot(live);DWORD error=0;const auto recovery=Recovery(data,L"2026-10-31_013731",error);
 Check(!recovery.empty()&&CtBackup::Detail::IsTreeTargetWithinLegacyPathBudget(live,recovery),"The actual application recovery destination preserves every valid current-tree path budget");
 CtBackup::StagedRestore staged;const auto stagedResult=CtBackup::StageValidated7zRestore(archive,root/L"boundary-stage",nullptr,nullptr,staged);Check(stagedResult.ok(),"The valid archive is actually extracted and sealed before the long-current-tree commit");if(!stagedResult.ok())throw std::runtime_error("Boundary stage unavailable");const auto stagedBefore=PrivateSnapshot(staged.stagingRoot),payloadBefore=PrivateSnapshot(staged.payloadDir);
 const auto committed=CtBackup::CommitStagedRestore(staged,live,recovery);longCommit=committed.ok();
 std::wcout<<L"LONG_RECOVERY="<<recovery.native()<<L"\nLONG_COMMIT_DETAILS="<<committed.details<<L'\n';
 Check(longCommit,"Actual SDK restore commits when current data already fits its final Sites location");
 if(longCommit){Check(PrivateSnapshot(recovery)==old&&PrivateSnapshot(live)==payloadBefore,"Successful compact recovery preserves exact old and installed bytes and object identities");Check(!fs::exists(staged.stagingRoot),"Successful restore removes only its completed staging wrapper");}
 else{Check(PrivateSnapshot(live)==old&&PrivateSnapshot(staged.stagingRoot)==stagedBefore&&!fs::exists(recovery),"A baseline path refusal preserves exact original live and sealed staged bytes and identities");}
 ++recoveryCases;const auto race=root/L"race";const auto raceLive=race/L"Sites";RecoveryWrite(raceLive/L"Previous/old.bin","race original");const auto raceOriginal=PrivateSnapshot(raceLive);const auto raceRecovery=Recovery(race,L"race",error);
 CtBackup::StagedRestore raceStage;const auto raceStaged=CtBackup::StageValidated7zRestore(archive,root/L"race-stage",nullptr,nullptr,raceStage);if(!raceStaged.ok())throw std::runtime_error("Race stage unavailable");const auto raceStageBefore=PrivateSnapshot(raceStage.stagingRoot);
 RecoveryWrite(raceRecovery/L"foreign.txt","appeared after allocation");const auto appeared=PrivateSnapshot(raceRecovery);const auto refused=CtBackup::CommitStagedRestore(raceStage,raceLive,raceRecovery);
 Check(!refused.ok()&&PrivateSnapshot(raceLive)==raceOriginal&&PrivateSnapshot(raceStage.stagingRoot)==raceStageBefore&&PrivateSnapshot(raceRecovery)==appeared,"A destination appearing after allocation is never overwritten and preserves both live and staged identities");
 ++recoveryCases;const auto rollback=root/L"rollback";const auto rollbackLive=rollback/L"Sites";RecoveryWrite(rollbackLive/L"Previous/old.bin","rollback original");const auto rollbackOriginal=PrivateSnapshot(rollbackLive);const auto rollbackRecovery=Recovery(rollback,L"rollback",error);
 CtBackup::StagedRestore rollbackStage;const auto rollbackStaged=CtBackup::StageValidated7zRestore(archive,root/L"rollback-stage",nullptr,nullptr,rollbackStage);if(!rollbackStaged.ok())throw std::runtime_error("Rollback stage unavailable");const auto rollbackStageBefore=PrivateSnapshot(rollbackStage.stagingRoot);
 CtBackup::FailRestoreRenameForTest(2);const auto failed=CtBackup::CommitStagedRestore(rollbackStage,rollbackLive,rollbackRecovery);CtBackup::FailRestoreRenameForTest(0);
 Check(!failed.ok()&&PrivateSnapshot(rollbackLive)==rollbackOriginal&&PrivateSnapshot(rollbackStage.stagingRoot)==rollbackStageBefore&&!fs::exists(rollbackRecovery),"Second native rename failure rolls exact original and staged bytes and identities back without orphan recovery");
 const auto retry=CtBackup::CommitStagedRestore(rollbackStage,rollbackLive,rollbackRecovery);Check(retry.ok()&&PrivateSnapshot(rollbackRecovery)==rollbackOriginal&&PrivateBytes(rollbackLive/L"Restored/state.bin")==newBytes,"The same sealed stage safely retries after exact rollback");
 Check(PrivateSnapshot(source)==sourceBefore&&PrivateObject(archive)==archiveBefore&&_7zTest7z(archive.c_str(),nullptr,nullptr)==S_OK,"Commit, race rejection and rollback/retry preserve the exact ordinary source and original valid archive");Audit();
}
static void CallerFailure(){
 ++recoveryCases;Seed(3);const auto original=PrivateSnapshot(g_sDataDir/L"Sites");fs::copy_file(fixture/L"ordinary.7z",g_sDataDir/L"private.7z");
 recovery_probe::Begin(recovery_probe::Mode::Occupied,g_sDataDir);GuiProfRestoreAll();recovery_probe::End();
 callerBeforeStage=pickerCalls==1&&stageCalls==0;
 Check(callerBeforeStage,"Actual restore caller refuses exhausted recovery allocation before staging any archive");
 Check(lastNotice.find(L"0x800700B7")!=std::wstring::npos&&g_bUiEnabled&&!g_bClientNotesBusy.load()&&!g_bArchiveTaskInProgress,"Recovery allocation failure reports its actual error and releases the original owner busy state");
 Check(PrivateSnapshot(g_sDataDir/L"Sites")==original,"Caller allocation refusal preserves exact existing private client bytes and identities");Audit();
}
int wmain(int argc,wchar_t**argv){
 HHOOK cbt=nullptr;std::vector<HWINEVENTHOOK> hooks;
 try{
  std::cout<<std::unitbuf;if(argc!=3||wcscmp(argv[1],L"--headless"))throw std::runtime_error("Explicit bound headless fixture required");
  const fs::path parent(CTSPACES_RECOVERY_FIXTURE_PARENT),requested(argv[2]);
  if(!parent.is_absolute()||!requested.is_absolute()||requested.parent_path()!=parent||requested.filename()!=L"f")throw std::runtime_error("Only the compile-bound fresh fixture child is allowed");
  for(auto path=requested;!path.empty();){const auto attrs=::GetFileAttributesW(path.c_str());if(attrs!=INVALID_FILE_ATTRIBUTES&&(attrs&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Fixture reparse ancestor refused");const auto next=path.parent_path();if(next==path)break;path=next;}
  if(fs::exists(requested)||!fs::create_directory(requested))throw std::runtime_error("Fresh fixture required; existing data retained");fixture=requested;
  cbt=SetWindowsHookExW(WH_CBT,BackgroundCbt,nullptr,GetCurrentThreadId());if(!cbt)throw std::runtime_error("Native activation veto unavailable");
  for(DWORD event:{EVENT_OBJECT_SHOW,EVENT_OBJECT_FOCUS,EVENT_SYSTEM_FOREGROUND}){const auto hook=SetWinEventHook(event,event,nullptr,Event,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);if(!hook)throw std::runtime_error("Native observer unavailable");hooks.push_back(hook);}
  g_hInst=GetModuleHandleW(nullptr);g_bQaInstance=true;g_sExeDir=GetCurrentExecutablePath().parent_path();ownerThread=GetCurrentThreadId();
  WNDCLASSW cls{};cls.lpfnWndProc=HiddenProc;cls.hInstance=g_hInst;cls.lpszClassName=L"RecoveryFolderPrivateHidden";if(!RegisterClassW(&cls))throw std::runtime_error("Hidden owner class unavailable");g_hGui=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"Private recovery proof",WS_OVERLAPPED,0,0,600,240,nullptr,nullptr,g_hInst,nullptr);if(!g_hGui)throw std::runtime_error("Hidden owner unavailable");
  AllocatorCases(fixture);BoundaryAndTransactions(fixture);CallerFailure();
  unsigned long long bytes=0;for(const auto& entry:fs::recursive_directory_iterator(fixture))if(entry.is_regular_file())bytes+=entry.file_size();Check(bytes<=2*1024*1024,"All ordinary on-disk private fixtures remain below two MiB");Audit();
  Check(!forbidden&&!nativeShows&&!nativeFocus&&!nativeForeground,"Recovery proof never shows, focuses, raises, launches, uses the clipboard or injects input");
  DestroyWindow(g_hGui);g_hGui=nullptr;for(auto hook:hooks)UnhookWinEvent(hook);hooks.clear();UnhookWindowsHookEx(cbt);cbt=nullptr;
  std::cout<<"CHECKS="<<checks<<"\nCASES="<<recoveryCases<<"\nFAILURES="<<failures<<"\nLONG_COMMIT="<<longCommit<<"\nCALLER_BEFORE_STAGE="<<callerBeforeStage<<"\nPHYSICAL_FIXTURE_BYTES="<<bytes<<"\nNATIVE_EVENTS="<<nativeShows<<'/'<<nativeFocus<<'/'<<nativeForeground<<"\nFORBIDDEN="<<forbidden<<"\nVERSION="<<CTSPACES_VERSION_TEXT<<"\nAPI_OCCUPANCY_SIMULATION_ONLY=1\n";
  return failures?1:0;
 }catch(const std::exception& error){recovery_probe::End();std::cerr<<"Recovery folder fixture error: "<<error.what()<<'\n';if(g_hGui)DestroyWindow(g_hGui);for(auto hook:hooks)UnhookWinEvent(hook);if(cbt)UnhookWindowsHookEx(cbt);return 2;}
}
