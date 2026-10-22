// Native filesystem-only regressions: no application entry point, windows,
// browser launch or input APIs. All fixtures remain in the unique audit folder.
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

static BOOL WINAPI BeforeActivityMove(LPCWSTR,LPCWSTR,DWORD);
static BOOL WINAPI BeforeActivityRename(HANDLE,FILE_INFO_BY_HANDLE_CLASS,LPVOID,DWORD);
static HANDLE WINAPI BeforeActivityCreate(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,
    DWORD,DWORD,HANDLE);
#define MoveFileExW BeforeActivityMove
#define SetFileInformationByHandle BeforeActivityRename
#define CreateFileW BeforeActivityCreate
#include "../ClientActivity.h"
#undef MoveFileExW
#undef SetFileInformationByHandle
#undef CreateFileW

namespace fs=std::filesystem;
static unsigned checks=0;
static unsigned mode=0;
static bool enabled=false,attempted=false,mutated=false;
static fs::path fixture,alpha,bravo,displaced,foreign,stageSeen;
static HANDLE targetLock=INVALID_HANDLE_VALUE;
static void Check(bool pass,const char* description) {
  ++checks;if(!pass)throw std::runtime_error(description);
}
static std::string ReadBytes(const fs::path& path) {
  std::ifstream stream(path,std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),{}};
}
static void Put(const fs::path& path,const std::string& value) {
  std::ofstream file(path,std::ios::binary);file<<value;
  Check(file.good(),"Write synthetic marker");
}
static void Interfere(const fs::path& source,const fs::path& target) {
  if(!enabled || !source.filename().native().starts_with(L".ctSpaces-activity-") ||
      target!=alpha/client_activity::kFileName)return;
  enabled=false;attempted=true;stageSeen=source;
  std::error_code error;
  if(mode==0) {
    fs::rename(alpha,displaced,error);
    if(!error)fs::rename(bravo,alpha,error);
    mutated=!error;
  } else {
    fs::rename(source,displaced,error);
    if(!error) {
      Put(source,"FOREIGN DOCUMENT MUST SURVIVE");foreign=source;mutated=true;
    }
    if(mode==2) {
      targetLock=::CreateFileW(target.c_str(),GENERIC_READ,FILE_SHARE_READ,
          nullptr,OPEN_EXISTING,0,nullptr);
      Check(targetLock!=INVALID_HANDLE_VALUE,"Hold exact target to reject native replacement");
    }
  }
}
static BOOL WINAPI BeforeActivityMove(LPCWSTR source,LPCWSTR target,DWORD flags) {
  Interfere(fs::path(source),fs::path(target));
  return ::MoveFileExW(source,target,flags);
}
static HANDLE WINAPI BeforeActivityCreate(LPCWSTR path,DWORD access,DWORD share,
    LPSECURITY_ATTRIBUTES security,DWORD disposition,DWORD flags,HANDLE source) {
  const fs::path original(path);
  if(mode==0 && enabled && disposition==CREATE_NEW &&
      original.filename().native().starts_with(L".ctSpaces-activity-"))
    Interfere(original,alpha/client_activity::kFileName);
  return ::CreateFileW(path,access,share,security,disposition,flags,source);
}
static BOOL WINAPI BeforeActivityRename(HANDLE object,FILE_INFO_BY_HANDLE_CLASS kind,
    LPVOID information,DWORD size) {
  if(kind==FileRenameInfo && information) {
    auto* rename=static_cast<FILE_RENAME_INFO*>(information);
    wchar_t path[32768]{};
    const DWORD count=GetFinalPathNameByHandleW(object,path,static_cast<DWORD>(std::size(path)),
        FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(count && count<std::size(path)) {
      std::wstring original(path,count);
      if(original.starts_with(L"\\\\?\\"))original.erase(0,4);
      Interfere(fs::path(original),fs::path(std::wstring(rename->FileName,
          rename->FileNameLength/sizeof(wchar_t))));
    }
  }
  return ::SetFileInformationByHandle(object,kind,information,size);
}
int wmain(int argc,wchar_t** argv) {
  fixture=fs::current_path()/L"build/app-audit-round5-2026-10-15/clients"/
      (L"fixture-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
  try {
    mode=argc==2?static_cast<unsigned>(_wtoi(argv[1])):0;
    Check(mode<3,"Choose one targeted filesystem race");
    alpha=fixture/L"Alpha";bravo=fixture/L"Bravo";displaced=fixture/L"displaced";
    fs::create_directories(alpha);fs::create_directories(bravo);
    const auto now=client_activity::Now();
    const client_activity::Record old{now-100000,false},other{now-200000,false},fresh{now,false};
    Check(client_activity::Write(alpha,old) && client_activity::Write(bravo,other),
          "Seed independent client activity histories");
    Put(alpha/L"notes","ALPHA PRIVATE NOTE");Put(bravo/L"notes","BRAVO PRIVATE NOTE");
    enabled=true;const bool saved=client_activity::Write(alpha,fresh);enabled=false;
    if(targetLock!=INVALID_HANDLE_VALUE){CloseHandle(targetLock);targetLock=INVALID_HANDLE_VALUE;}
    const bool wrongClient=mode==0 && mutated && saved &&
        client_activity::Read(alpha)==fresh && ReadBytes(alpha/L"notes")=="BRAVO PRIVATE NOTE";
    const bool oldHistoryLost=mode==1 && mutated && client_activity::Read(alpha)!=old;
    const bool foreignCleanupLost=mode==2 && mutated && !fs::exists(foreign);
    std::cout<<"MODE="<<mode<<" ATTEMPTED="<<attempted<<" MUTATED="<<mutated
        <<" SAVED="<<saved<<" WRONG_CLIENT_ACTIVITY="<<wrongClient
        <<" OLD_HISTORY_LOST="<<oldHistoryLost<<" FOREIGN_CLEANUP_LOST="<<foreignCleanupLost<<'\n';
    std::wcout<<L"Fixture retained: "<<fixture<<L'\n';
    Check(attempted,"Native publication boundary actually reached");
    Check(!wrongClient,"Activity update never commits to a different client");
    Check(!oldHistoryLost,"Unbound foreign stage never replaces valid history");
    Check(!foreignCleanupLost,"Failure cleanup never deletes a foreign stage");
    Check(!mutated,"Held client and output object prevent actual namespace substitution");
    Check(ReadBytes(alpha/L"notes")=="ALPHA PRIVATE NOTE" &&
        ReadBytes(bravo/L"notes")=="BRAVO PRIVATE NOTE" && client_activity::Read(bravo)==other,
          "Independent private data and Bravo activity remain unchanged");
    Check(mode==2?!saved && client_activity::Read(alpha)==old:
        saved && client_activity::Read(alpha)==fresh,
          "Native failure preserves old history and ordinary publication succeeds");
    for(const auto& item:fs::directory_iterator(alpha))
      Check(!item.path().filename().native().starts_with(L".ctSpaces-activity-"),
            "No owned staging file remains after normal or blocked publication");
    std::cout<<"Client activity boundary: "<<checks<<" assertions passed\n";
    return 0;
  } catch(const std::exception& error) {
    if(targetLock!=INVALID_HANDLE_VALUE)CloseHandle(targetLock);
    std::cerr<<"FAIL: "<<error.what()<<" after "<<checks<<" assertions\n";
    std::wcout<<L"Fixture retained: "<<fixture<<L'\n';return 1;
  }
}
