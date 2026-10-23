// Focused profile-creation ownership regressions. This suite never starts the app
// entry point, creates visible windows, or permits foreground/input APIs.
#include <windows.h>
#include <shellapi.h>

static unsigned deniedPresentationCalls = 0;
static unsigned deniedLaunchCalls = 0;
static BOOL WINAPI BoundaryNoShowWindow(HWND, int) {
  ++deniedPresentationCalls; return FALSE;
}
static HWND WINAPI BoundaryNoSetFocus(HWND) {
  ++deniedPresentationCalls; return nullptr;
}
static BOOL WINAPI BoundaryNoSetForegroundWindow(HWND) {
  ++deniedPresentationCalls; return FALSE;
}
static HWND WINAPI BoundaryNoSetActiveWindow(HWND) {
  ++deniedPresentationCalls; return nullptr;
}
static BOOL WINAPI BoundarySafeSetWindowPos(HWND window, HWND after, int x,
    int y, int width, int height, UINT flags) {
  if (!(flags & SWP_NOACTIVATE) || (flags & SWP_SHOWWINDOW)) {
    ++deniedPresentationCalls; return FALSE;
  }
  return ::SetWindowPos(window, after, x, y, width, height, flags);
}
static HDWP WINAPI BoundarySafeDeferWindowPos(HDWP defer, HWND window,
    HWND after, int x, int y, int width, int height, UINT flags) {
  if (!(flags & SWP_NOACTIVATE) || (flags & SWP_SHOWWINDOW)) {
    ++deniedPresentationCalls; return nullptr;
  }
  return ::DeferWindowPos(defer, window, after, x, y, width, height, flags);
}
static HINSTANCE WINAPI BoundaryNoShellExecuteW(HWND, LPCWSTR, LPCWSTR,
    LPCWSTR, LPCWSTR, INT) {
  ++deniedLaunchCalls; return reinterpret_cast<HINSTANCE>(ERROR_ACCESS_DENIED);
}
static BOOL WINAPI BoundaryNoShellExecuteExW(SHELLEXECUTEINFOW *) {
  ++deniedLaunchCalls; SetLastError(ERROR_ACCESS_DENIED); return FALSE;
}
static BOOL WINAPI BoundaryNoCreateProcessW(LPCWSTR, LPWSTR,
    LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID,
    LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION) {
  ++deniedLaunchCalls; SetLastError(ERROR_ACCESS_DENIED); return FALSE;
}
#define ShowWindow BoundaryNoShowWindow
#define SetFocus BoundaryNoSetFocus
#define SetForegroundWindow BoundaryNoSetForegroundWindow
#define SetActiveWindow BoundaryNoSetActiveWindow
#define SetWindowPos BoundarySafeSetWindowPos
#define DeferWindowPos BoundarySafeDeferWindowPos
#define ShellExecuteW BoundaryNoShellExecuteW
#define ShellExecuteExW BoundaryNoShellExecuteExW
#define CreateProcessW BoundaryNoCreateProcessW
static BOOL WINAPI R4BeforeCreateMove(LPCWSTR source,LPCWSTR destination,DWORD flags);
static BOOL WINAPI R4BeforeBoundMove(HANDLE object,FILE_INFO_BY_HANDLE_CLASS kind,
    LPVOID information,DWORD size);
static HANDLE WINAPI R4BeforeSourceBinding(LPCWSTR path,DWORD access,DWORD share,
    LPSECURITY_ATTRIBUTES security,DWORD disposition,DWORD flags,HANDLE templateFile);
#define MoveFileExW R4BeforeCreateMove
#define SetFileInformationByHandle R4BeforeBoundMove
#define CreateFileW R4BeforeSourceBinding
#define wmain ExistingR4IsolationFixtureEntry
#include "ClientIsolationTests.cpp"
#undef wmain
#undef MoveFileExW
#undef SetFileInformationByHandle
#undef CreateFileW

enum class R4CreationFault { None, ClientRootSwap, SourceSlotSwap, ForeignCleanup,
    DestinationCollision, NewClientParentSwap, SourceBeforeBind };
static R4CreationFault creationFault=R4CreationFault::None;
static bool creationMutationAttempted=false,creationMutationSucceeded=false;
static fs::path creationPreserved,creationForeign,creationOriginalClient;
static fs::path creationWorkRoot,creationCollisionPath;
static bool creationNewClient=false;

static void R4MutateCreationBoundary(const fs::path& from,const fs::path& to) {
  const bool selected=creationNewClient?
      from.filename()==L"Client" && to.filename()==L"Charlie":
      from.filename()==L"Slot" && to.filename()==L"firefox";
  if(creationFault==R4CreationFault::None || !selected ||
      from.native().find(L"_ProfileCreate")==std::wstring::npos)
    return;
  creationMutationAttempted=true;
  creationWorkRoot=from.parent_path();
  const auto mode=creationFault;creationFault=R4CreationFault::None;
  std::error_code error;
  if(mode==R4CreationFault::ClientRootSwap) {
    fs::rename(creationOriginalClient,creationPreserved,error);
    if(!error)fs::rename(otherRoot,creationOriginalClient,error);
    creationMutationSucceeded=!error;
  } else if(mode==R4CreationFault::NewClientParentSwap) {
    fs::rename(to.parent_path(),creationPreserved,error);
    if(!error)fs::create_directory(to.parent_path());
    creationMutationSucceeded=!error;
  } else if(mode==R4CreationFault::DestinationCollision) {
    creationForeign=from.parent_path()/L"foreign-document.txt";
    Put(creationForeign,"FOREIGN DOCUMENT MUST SURVIVE");
    creationCollisionPath=to;
    fs::create_directory(to);
    Put(to/L"foreign-target.txt","FOREIGN TARGET MUST SURVIVE");
    creationMutationSucceeded=true;
  } else if(mode==R4CreationFault::SourceSlotSwap || mode==R4CreationFault::SourceBeforeBind) {
    fs::rename(from,creationPreserved,error);
    if(!error)fs::rename(creationForeign,from,error);
    creationMutationSucceeded=!error;
  } else {
    fs::rename(from.parent_path(),creationPreserved,error);
    if(!error) {
      fs::create_directories(from.parent_path());
      creationForeign=from.parent_path()/L"foreign-document.txt";
      Put(creationForeign,"FOREIGN DOCUMENT MUST SURVIVE");
    }
    creationMutationSucceeded=!error;
  }
}
static HANDLE WINAPI R4BeforeSourceBinding(LPCWSTR path,DWORD access,DWORD share,
    LPSECURITY_ATTRIBUTES security,DWORD disposition,DWORD flags,HANDLE templateFile) {
  const fs::path original(path);
  if(creationFault==R4CreationFault::SourceBeforeBind &&
      (access&FILE_LIST_DIRECTORY) && !(access&DELETE) &&
      (flags&FILE_FLAG_BACKUP_SEMANTICS) && disposition==OPEN_EXISTING)
    R4MutateCreationBoundary(original,creationNewClient?g_sDataDir/L"Sites/Charlie":
        creationOriginalClient/L"Browsers/firefox");
  return ::CreateFileW(path,access,share,security,disposition,flags,templateFile);
}
static BOOL WINAPI R4BeforeCreateMove(LPCWSTR source,LPCWSTR destination,DWORD flags) {
  R4MutateCreationBoundary(fs::path(source),fs::path(destination));
  return ::MoveFileExW(source,destination,flags);
}
static BOOL WINAPI R4BeforeBoundMove(HANDLE object,FILE_INFO_BY_HANDLE_CLASS kind,
    LPVOID information,DWORD size) {
  if(kind==FileRenameInfo && information) {
    auto* rename=static_cast<FILE_RENAME_INFO*>(information);
    wchar_t source[32768]{};
    const DWORD length=GetFinalPathNameByHandleW(object,source,
        static_cast<DWORD>(std::size(source)),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(length && length<std::size(source)) {
      std::wstring from(source,length);
      if(from.starts_with(L"\\\\?\\"))from.erase(0,4);
      R4MutateCreationBoundary(fs::path(from),fs::path(std::wstring(
          rename->FileName,rename->FileNameLength/sizeof(wchar_t))));
    }
  }
  return ::SetFileInformationByHandle(object,kind,information,size);
}

static void R4CreationOwnershipCase(unsigned mode) {
  const auto caseName=L"c"+std::to_wstring(mode);Setup(caseName.c_str());
  if(mode==6) {
    g_sDataDir=fixture/caseName/L"empty";fs::create_directories(g_sDataDir);
    otherRoot=g_sDataDir/L"Sites"/L"Bravo";
  }
  creationNewClient=mode>=4 && mode!=8;
  creationOriginalClient=g_sDataDir/L"Sites"/L"Alpha";
  creationPreserved=g_sDataDir/L"preserved-original";
  creationMutationAttempted=creationMutationSucceeded=false;
  creationWorkRoot.clear();creationForeign.clear();creationCollisionPath.clear();
  const auto alphaBefore=ReadBytes(creationOriginalClient/client_notes::kNotebookFileName);
  const auto bravoBefore=ReadBytes(otherRoot/client_notes::kNotebookFileName);
  if(mode==1 || mode==4 || mode==8 || mode==9) {
    creationForeign=otherRoot/L"Browsers"/L"firefox";
    Check(CreateStagedBrowserSlot(creationForeign,BrowserKind::Firefox,L"Native fixture"),
          "Seed independent Firefox browser slot without launching a browser");
    Put(creationForeign/L"Profile"/L"private-session","BRAVO PRIVATE FIREFOX");
    if(mode==4 || mode==9)creationForeign=otherRoot;
  }
  creationFault=mode==0?R4CreationFault::ClientRootSwap:
      mode==1 || mode==4?R4CreationFault::SourceSlotSwap:
      mode==2?R4CreationFault::ForeignCleanup:
      mode==3 || mode==7?R4CreationFault::DestinationCollision:
      mode==5?R4CreationFault::NewClientParentSwap:
      mode==8 || mode==9?R4CreationFault::SourceBeforeBind:R4CreationFault::None;
  fs::path profile;std::wstring error;
  const bool result=creationNewClient?CreateNewV2Client(L"Charlie",BrowserKind::Firefox,profile,error):
      EnsureBrowserSlotForClient(L"Alpha",creationOriginalClient,BrowserKind::Firefox,profile,error);
  const bool wrongClientSlot=mode==0 && result &&
      ReadBytes(creationOriginalClient/client_notes::kNotebookFileName)==bravoBefore &&
      fs::is_directory(creationOriginalClient/L"Browsers"/L"firefox");
  const bool wrongSourceSlot=(mode==1 || mode==8) && result &&
      ReadBytes(profile/L"private-session")=="BRAVO PRIVATE FIREFOX";
  const bool wrongNewClient=(mode==4 || mode==9) && result &&
      ReadBytes(g_sDataDir/L"Sites"/L"Charlie"/client_notes::kNotebookFileName)==bravoBefore;
  const bool foreignCleanupLost=mode==2 && creationMutationSucceeded &&
      !fs::exists(creationForeign);
  std::wcout<<L"MODE="<<mode<<L" ATTEMPTED="<<creationMutationAttempted
      <<L" MUTATED="<<creationMutationSucceeded<<L" RESULT_OK="<<result
      <<L" WRONG_CLIENT_SLOT="<<wrongClientSlot<<L" WRONG_SOURCE_SLOT="<<wrongSourceSlot
      <<L" WRONG_NEW_CLIENT="<<wrongNewClient
      <<L" FOREIGN_CLEANUP_LOST="<<foreignCleanupLost<<L"\nDETAILS="<<error<<L"\n";
  Check(mode==6 || creationMutationAttempted,
        "Focused fault reached the actual source-binding or final native move boundary");
  if(mode==0) {
    Check(ReadBytes(creationPreserved/client_notes::kNotebookFileName)==alphaBefore ||
        ReadBytes(creationOriginalClient/client_notes::kNotebookFileName)==alphaBefore,
        "Original Alpha rich notebook remains available");
  } else if(mode!=6 && mode!=9) {
    Check(ReadBytes(creationOriginalClient/client_notes::kNotebookFileName)==alphaBefore &&
        ReadBytes(otherRoot/client_notes::kNotebookFileName)==bravoBefore,
        "Selected and unrelated rich notebook bytes remain intact");
  }
  Check(!wrongClientSlot && !wrongSourceSlot && !wrongNewClient && !foreignCleanupLost,
        "Profile construction never accepts another client/source or cleans foreign contents");
  if(mode==3 || mode==7) {
    Check(creationMutationSucceeded && !result,
          "A competing destination really causes native no-replace publication to fail");
    Check(ReadBytes(creationForeign)=="FOREIGN DOCUMENT MUST SURVIVE" &&
          ReadBytes(creationCollisionPath/L"foreign-target.txt")=="FOREIGN TARGET MUST SURVIVE",
          "Failed construction preserves independent stage and destination bytes");
    Check(fs::is_directory(creationWorkRoot) && error.find(creationWorkRoot.wstring())!=std::wstring::npos,
          "Failed nonempty stage is retained and reports its exact path");
  } else if(mode==8 || mode==9) {
    Check(creationMutationSucceeded && !result,
          "A nonempty foreign source swapped before first binding is rejected before construction");
    const auto retainedSource=creationWorkRoot/(mode==8?L"Slot":L"Client");
    const auto retainedProfile=mode==8?retainedSource/L"Profile":
        retainedSource/L"Browsers/firefox/Profile";
    Check(ReadBytes(retainedProfile/L"private-session")=="BRAVO PRIVATE FIREFOX",
          "Rejected first-binding replacement retains its foreign Firefox bytes");
    Check(fs::is_directory(creationWorkRoot) &&
        error.find(creationWorkRoot.wstring())!=std::wstring::npos,
          "Rejected foreign stage remains available at the reported exact path");
    Check(ReadBytes(creationOriginalClient/client_notes::kNotebookFileName)==alphaBefore,
          "Rejected early source substitution preserves Alpha's notebook");
    if(mode==8) {
      Check(!fs::exists(creationOriginalClient/L"Browsers/firefox"),
            "Rejected foreign slot is never published into Alpha");
      Check(fs::is_empty(creationPreserved),
            "The displaced original stage was still empty before any payload construction");
    } else {
      Check(!fs::exists(g_sDataDir/L"Sites/Charlie"),
            "Rejected foreign client is never published as Charlie");
      Check(fs::is_empty(creationPreserved),
            "The displaced original client was still empty before markers or profile construction");
      Check(ReadBytes(retainedSource/client_notes::kNotebookFileName)==bravoBefore,
            "Rejected whole-client replacement preserves its exact rich notebook bytes");
      const auto originalOtherRoot=otherRoot;
      otherRoot=retainedSource;OtherIntact();otherRoot=originalOtherRoot;
    }
  } else {
    Check(result,"Blocked substitutions do not prevent ordinary valid profile creation");
    Check(!fs::exists(g_sDataDir/L"_ProfileCreate"),
          "Successful creation removes the original empty work root and temporary sentinel");
    if(!creationWorkRoot.empty())Check(!fs::exists(creationWorkRoot),
          "Successful commit never leaves its original staged directory behind");
    Check(fs::is_regular_file(profile/L"user.js"),
          "Constructed Firefox profile retains its startup preferences");
    for(const auto& entry:fs::directory_iterator(g_sDataDir/L"Sites"))
      Check(entry.is_directory() && CtBackup::Detail::IsValidClientDirectoryName(entry.path().filename().native()),
            "Creation guards leave no sentinel that could poison a later backup");
  }
  if(mode!=6 && mode!=9)OtherIntact();
}

int wmain(int argc,wchar_t** argv) {
  fixture=fs::current_path()/L"build"/(L"r4c-"+std::to_wstring(GetCurrentProcessId())+
      L"-"+std::to_wstring(GetTickCount64()));
  try {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);g_hInst=GetModuleHandleW(nullptr);
    g_bQaInstance=true;g_sExeDir=GetCurrentExecutablePath().parent_path();
    g_hGui=CreateWindowExW(0,L"STATIC",L"Creation ownership message-only fixture",0,
        0,0,1,1,HWND_MESSAGE,nullptr,g_hInst,nullptr);
    const unsigned mode=argc==2?static_cast<unsigned>(_wtoi(argv[1])):0;
    Check(mode<10,"Select one explicit disposable creation boundary");
    R4CreationOwnershipCase(mode);
    Check(deniedLaunchCalls==0 && deniedPresentationCalls==0 && !IsWindowVisible(g_hGui),
          "Focused native case requests no launch, focus or presentation and stays invisible");
    DestroyWindow(g_hGui);g_hGui=nullptr;CoUninitialize();
    std::wcout<<L"Client creation ownership: "<<checks<<L" checks passed; fixture="<<fixture<<L"\n";
    return 0;
  } catch(const std::exception& error) {
    std::cerr<<"FAIL: "<<error.what()<<'\n';
    std::wcout<<L"Fixture retained: "<<fixture<<L"\n";
    if(g_hGui)DestroyWindow(g_hGui);CoUninitialize();return 1;
  }
}
