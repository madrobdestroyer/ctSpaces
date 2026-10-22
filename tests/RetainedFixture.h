#pragma once
#include <windows.h>
#include <objbase.h>
#include <filesystem>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>

// Retained QA evidence outlives a process ID. Allocate a new directory with
// one atomic create operation instead of reusing or deleting an earlier run.
namespace retained_fixture {
inline std::filesystem::path CreateRoot(const std::filesystem::path &parent,
    const std::wstring &prefix,
    const std::function<std::wstring()> &nextName={}) {
  namespace fs=std::filesystem;
  const auto validLeaf=[](const std::wstring &name) {
    const fs::path leaf(name);
    return !name.empty()&&!leaf.has_parent_path()&&!leaf.is_absolute()&&leaf!=L"."&&leaf!=L"..";
  };
  if(!validLeaf(prefix))throw std::runtime_error("Private fixture prefix must be one directory name");
  std::error_code error;
  fs::create_directories(parent,error);
  if(error)throw fs::filesystem_error("Private fixture parent is unavailable",parent,error);
  for(unsigned attempt=0;attempt<16;++attempt) {
    std::wstring name;
    if(nextName)name=nextName();
    else {
      GUID id{};wchar_t text[40]{};
      if(FAILED(CoCreateGuid(&id))||!StringFromGUID2(id,text,static_cast<int>(std::size(text))))
        throw std::runtime_error("Private fixture identity is unavailable");
      name=prefix+L"-"+std::to_wstring(GetCurrentProcessId())+L"-"+text;
    }
    if(!validLeaf(name))throw std::runtime_error("Private fixture identity must be one directory name");
    const auto candidate=parent/name;error.clear();
    if(fs::create_directory(candidate,error))return candidate;
    if(error&&error!=std::errc::file_exists)
      throw fs::filesystem_error("Private fixture could not be created",candidate,error);
  }
  throw std::runtime_error("Private fixture collision limit reached");
}
}
