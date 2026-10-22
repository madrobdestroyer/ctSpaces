// Filesystem-only deletion regression. No application, windows, input or launch APIs.
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
static BOOL WINAPI BeforeDeleteDisposition(HANDLE,FILE_INFO_BY_HANDLE_CLASS,LPVOID,DWORD);
#define SetFileInformationByHandle BeforeDeleteDisposition
#include "../ClientDeletionSafety.h"
#undef SetFileInformationByHandle

namespace fs=std::filesystem;
static unsigned checks=0;
static bool enabled=false,attempted=false,wrote=false;
static DWORD writerError=ERROR_SUCCESS;
static fs::path leaf;
static void Check(bool pass,const char* description) {
  ++checks;if(!pass)throw std::runtime_error(description);
}
static std::string ReadBytes(const fs::path& path) {
  std::ifstream file(path,std::ios::binary);
  return {std::istreambuf_iterator<char>(file),{}};
}
static BOOL WINAPI BeforeDeleteDisposition(HANDLE object,FILE_INFO_BY_HANDLE_CLASS kind,
    LPVOID information,DWORD size) {
  if(enabled && kind==FileDispositionInfo) {
    enabled=false;attempted=true;
    HANDLE writer=CreateFileW(leaf.c_str(),GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,nullptr);
    writerError=writer==INVALID_HANDLE_VALUE?GetLastError():ERROR_SUCCESS;
    if(writer!=INVALID_HANDLE_VALUE) {
      DWORD written=0;
      wrote=WriteFile(writer,"NEW CONTENT",11,&written,nullptr) && written==11 && FlushFileBuffers(writer);
      CloseHandle(writer);
    }
  }
  return ::SetFileInformationByHandle(object,kind,information,size);
}
int main() {
  const auto fixture=fs::current_path()/L"build/app-audit-round5-2026-10-15/clients"/
      (L"writer-fixture-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
  try {
    fs::create_directories(fixture);
    const auto root=fixture/L"Alpha";fs::create_directory(root);
    leaf=root/L"external-draft.txt";
    {std::ofstream file(leaf,std::ios::binary);file<<"OLD CONTENT";Check(file.good(),"Seed original draft");}
    const auto other=fixture/L"Bravo.txt";
    {std::ofstream file(other,std::ios::binary);file<<"BRAVO PRIVATE";Check(file.good(),"Seed unrelated client sentinel");}
    client_deletion_safety::BoundTree bound;
    std::wstring error;
    Check(bound.Capture(root,error) && bound.Verify(error),
          "Bind and verify exact original client data before deletion confirmation");
    enabled=true;const bool removed=bound.Remove(leaf,error);enabled=false;
    const bool lost=wrote && !fs::exists(leaf);
    std::cout<<"ATTEMPTED="<<attempted<<" WRITER_OPEN_ERROR="<<writerError
        <<" WROTE_AFTER_CHECK="<<wrote<<" REMOVE_OK="<<removed<<" WRITTEN_DATA_LOST="<<lost<<'\n';
    std::wcout<<L"DETAILS="<<error<<L"\nFixture retained: "<<fixture<<L'\n';
    Check(attempted,"Writer attempts the actual post-check disposition boundary");
    Check(!lost,"Deletion never erases a new edit written after the final metadata check");
    Check(!wrote && writerError==ERROR_SHARING_VIOLATION,
          "Exact deletion handle excludes late writers throughout disposition");
    Check(removed && !fs::exists(leaf),"Authorized original file can still be deleted");
    Check(ReadBytes(other)=="BRAVO PRIVATE","Unrelated client sentinel remains intact");
    std::cout<<"Client deletion writer boundary: "<<checks<<" assertions passed\n";
    return 0;
  } catch(const std::exception& error) {
    std::cerr<<"FAIL: "<<error.what()<<" after "<<checks<<" assertions\n";
    std::wcout<<L"Fixture retained: "<<fixture<<L'\n';return 1;
  }
}
