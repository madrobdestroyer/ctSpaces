#pragma once
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace client_deletion_safety {
namespace fs = std::filesystem;
#ifdef CTSPACES_CLIENT_DELETE_TEST_HOOKS
void BeforeMetadataMove(const fs::path &root);
void BeforeMetadataCleanup(const fs::path &root);
#endif
#ifdef CTSPACES_CLIENT_NAMESPACE_TEST_HOOKS
void BeforeNamespaceMove(const fs::path &source, const fs::path &destination, HANDLE original);
void BeforeNamespaceUpgrade(const fs::path &source);
#endif

// A folder name alone is not deletion authority: another process can move a
// different client's tree into it while a confirmation or preflight runs.
// Keep directory namespaces pinned and delete only the captured file objects.
class BoundTree {
  struct Entry {
    fs::path relative;
    HANDLE directory = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION identity{};
    bool removed = false;
    bool directoryDeleteAccess = false;
  };
  struct Ancestor { fs::path path; HANDLE handle = INVALID_HANDLE_VALUE; BY_HANDLE_FILE_INFORMATION identity{}; };
  struct Sentinel { fs::path parent; HANDLE handle = INVALID_HANDLE_VALUE; };
  struct PathLess {
    bool operator()(const fs::path &a, const fs::path &b) const {
      return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE) == CSTR_LESS_THAN;
    }
  };
  fs::path root_;
  std::vector<Entry> entries_;
  std::map<fs::path,size_t,PathLess> index_;
  std::vector<Ancestor> ancestors_;
  std::vector<Sentinel> sentinels_;
  bool movable_ = false;
  bool namespaceOnly_ = false;

  static bool SamePath(const fs::path &a, const fs::path &b) {
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
  }
  static bool SameObject(const BY_HANDLE_FILE_INFORMATION &a,
                         const BY_HANDLE_FILE_INFORMATION &b) {
    return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
           a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow &&
           (a.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ==
               (b.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
  }
  static bool SameFile(const BY_HANDLE_FILE_INFORMATION &a,
                       const BY_HANDLE_FILE_INFORMATION &b) {
    return SameObject(a,b) && a.nFileSizeHigh == b.nFileSizeHigh &&
           a.nFileSizeLow == b.nFileSizeLow &&
           CompareFileTime(&a.ftLastWriteTime,&b.ftLastWriteTime) == 0;
  }
  static bool Read(HANDLE handle, BY_HANDLE_FILE_INFORMATION &info, bool directory) {
    if(!GetFileInformationByHandle(handle,&info)) return false;
    if((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory) {
      SetLastError(ERROR_FILE_INVALID);return false;
    }
    return true;
  }
  static bool Fail(std::wstring &error, const wchar_t *action, const fs::path &path,
                   DWORD code = GetLastError()) {
    error = std::wstring(action) + L": " + path.wstring() +
            L" (Windows error " + std::to_wstring(code) + L").";
    return false;
  }
  Entry *Find(const fs::path &relative) {
    auto found=index_.find(relative);
    return found == index_.end() ? nullptr : &entries_[found->second];
  }
  bool CaptureEntry(const fs::path &path, bool directory, std::wstring &error,
                    bool directoryDeleteAccess = true) {
    HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | (directory ? FILE_LIST_DIRECTORY : 0) | (directory && directoryDeleteAccess ? DELETE : 0),
        FILE_SHARE_READ | FILE_SHARE_WRITE | (directory ? 0 : FILE_SHARE_DELETE),
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if (handle == INVALID_HANDLE_VALUE) return Fail(error,L"A client object could not be pinned",path);
    BY_HANDLE_FILE_INFORMATION info{};
    const bool valid = Read(handle,info,directory);
    if (!valid) { const DWORD code=GetLastError();CloseHandle(handle);return Fail(error,L"A client object changed type or became unsafe",path,code); }
    Entry entry{path == root_ ? fs::path{} : path.lexically_relative(root_),
                directory ? handle : INVALID_HANDLE_VALUE,info,false,directory && directoryDeleteAccess};
    if (!directory) CloseHandle(handle);
    try {
      if(!index_.emplace(entry.relative,entries_.size()).second) {
        if(directory) CloseHandle(handle);
        return Fail(error,L"Ambiguous client object names cannot authorize deletion",path,ERROR_FILE_INVALID);
      }
      entries_.push_back(std::move(entry));
    }
    catch (...) { if (directory) CloseHandle(handle); throw; }
    return true;
  }

public:
  BoundTree() = default;
  BoundTree(const BoundTree &) = delete;
  BoundTree &operator=(const BoundTree &) = delete;
  ~BoundTree() {
    for (auto &entry : entries_) if (entry.directory != INVALID_HANDLE_VALUE) CloseHandle(entry.directory);
    for (auto &sentinel : sentinels_) if (sentinel.handle != INVALID_HANDLE_VALUE) CloseHandle(sentinel.handle);
    for (auto &ancestor : ancestors_) if (ancestor.handle != INVALID_HANDLE_VALUE) CloseHandle(ancestor.handle);
  }

  bool PinAncestor(const fs::path &path, bool deleteAccess, std::wstring &error) {
    const auto absolute = fs::absolute(path).lexically_normal();
    if (std::any_of(ancestors_.begin(),ancestors_.end(),[&](const Ancestor &entry) { return SamePath(entry.path,absolute); })) return true;
    HANDLE handle=CreateFileW(absolute.c_str(),FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY | (deleteAccess ? DELETE : 0),
        FILE_SHARE_READ | FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if (handle == INVALID_HANDLE_VALUE) return Fail(error,L"A client directory ancestor could not be pinned",absolute);
    BY_HANDLE_FILE_INFORMATION info{};
    if (!Read(handle,info,true)) { const DWORD code=GetLastError();CloseHandle(handle);return Fail(error,L"A client directory ancestor is unsafe",absolute,code); }
    try { ancestors_.push_back({absolute,handle,info}); }
    catch (...) { CloseHandle(handle);throw; }
    return true;
  }
  bool ProtectEmptyParent(const fs::path &path, std::wstring &error) {
    const auto absolute=fs::absolute(path).lexically_normal();
    if(std::any_of(sentinels_.begin(),sentinels_.end(),[&](const Sentinel &entry) {
      return entry.handle != INVALID_HANDLE_VALUE && SamePath(entry.parent,absolute);
    })) return true;
    const auto ancestor=std::find_if(ancestors_.begin(),ancestors_.end(),[&](const Ancestor &entry) {
      return entry.handle != INVALID_HANDLE_VALUE && SamePath(entry.path,absolute);
    });
    if(ancestor == ancestors_.end()) return Fail(error,L"The client staging parent has no original namespace guard",absolute,ERROR_FILE_INVALID);
    // Empty NTFS directories can become junctions without changing their file
    // ID. First exclude directory writers, then hold a nonempty, exclusively
    // bound sentinel before returning to the native-compatible sharing mode.
    HANDLE tight=CreateFileW(absolute.c_str(),FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(tight == INVALID_HANDLE_VALUE) return Fail(error,L"The client staging parent could not exclude namespace changes",absolute);
    BY_HANDLE_FILE_INFORMATION info{};
    if(!Read(tight,info,true) || !SameObject(info,ancestor->identity)) {
      CloseHandle(tight);return Fail(error,L"The client staging parent was replaced before it could be protected",absolute,ERROR_FILE_INVALID);
    }
    for(unsigned attempt=0;attempt<128;++attempt) {
      const auto leaf=absolute/(L"~d"+std::to_wstring(attempt));
      HANDLE sentinel=CreateFileW(leaf.c_str(),GENERIC_READ | DELETE,FILE_SHARE_READ,
          nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
      if(sentinel == INVALID_HANDLE_VALUE) {
        const DWORD code=GetLastError();
        if(code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS) continue;
        CloseHandle(tight);return Fail(error,L"The client staging parent protection file could not be reserved",leaf,code);
      }
      BY_HANDLE_FILE_INFORMATION leafInfo{};
      if(!Read(sentinel,leafInfo,false) || !Read(tight,info,true) || !SameObject(info,ancestor->identity)) {
        CloseHandle(sentinel);CloseHandle(tight);
        return Fail(error,L"The client staging parent changed while reserving its protection file",absolute,ERROR_FILE_INVALID);
      }
      try { sentinels_.push_back({absolute,sentinel}); }
      catch(...) { CloseHandle(sentinel);CloseHandle(tight);throw; }
      CloseHandle(tight);return true;
    }
    CloseHandle(tight);return Fail(error,L"The client staging parent has no unused protection filename",absolute,ERROR_ALREADY_EXISTS);
  }
  bool Capture(const fs::path &root, std::wstring &error) {
    if(!CaptureNamespace(root,error)) return false;
    return CaptureContents(error);
  }
  bool CaptureContents(std::wstring &error) {
    if(!UpgradeRoot(error)) return false;
    namespaceOnly_=false;
    try {
      for (const auto &item:fs::recursive_directory_iterator(root_)) {
        const DWORD attributes=GetFileAttributesW(item.path().c_str());
        if(attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
          return Fail(error,L"A client tree entry is unsafe",item.path());
        if(!CaptureEntry(item.path(),(attributes & FILE_ATTRIBUTE_DIRECTORY) != 0,error)) return false;
      }
    } catch(const std::exception &) { return Fail(error,L"The client tree changed during capture",root_); }
    return Verify(error);
  }
  bool CaptureNamespace(const fs::path &root, std::wstring &error) {
    namespaceOnly_=true;
    root_=fs::absolute(root).lexically_normal();
    std::vector<fs::path> parents;
    for (auto path=root_.parent_path(); !path.empty();) {
      parents.push_back(path); const auto parent=path.parent_path();if(parent == path) break;path=parent;
    }
    std::reverse(parents.begin(),parents.end());
    for (const auto &parent:parents) if(!PinAncestor(parent,false,error)) return false;
    // Read-only namespace pins are compatible with the Notes save lease.
    // Hold this original identity while matching notes flush and close, then
    // require the identical object when acquiring DELETE access for a move.
    if(!CaptureEntry(root_,true,error,false)) return false;
    return Verify(error);
  }
  bool UpgradeRoot(std::wstring &error) {
    Entry *root=Find(fs::path{});
    if(!root || root->removed || root->directory == INVALID_HANDLE_VALUE)
      return Fail(error,L"The original client directory guard is unavailable",root_,ERROR_FILE_INVALID);
    if(root->directoryDeleteAccess) return true;
    CloseHandle(root->directory);root->directory=INVALID_HANDLE_VALUE;
#ifdef CTSPACES_CLIENT_NAMESPACE_TEST_HOOKS
    BeforeNamespaceUpgrade(root_);
#endif
    HANDLE handle=CreateFileW(root_.c_str(),DELETE | FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle == INVALID_HANDLE_VALUE) return Fail(error,L"The original client directory could not acquire DELETE access",root_);
    BY_HANDLE_FILE_INFORMATION info{};
    if(!Read(handle,info,true) || !SameObject(info,root->identity)) {
      CloseHandle(handle);
      return Fail(error,L"The original client directory was replaced before DELETE access was acquired",root_,ERROR_FILE_INVALID);
    }
    root->directory=handle;root->directoryDeleteAccess=true;return true;
  }
  bool Verify(std::wstring &error) {
    for(const auto &ancestor:ancestors_) {
      if(ancestor.handle == INVALID_HANDLE_VALUE) continue;
      BY_HANDLE_FILE_INFORMATION info{};
      if(!Read(ancestor.handle,info,true) || !SameObject(info,ancestor.identity))
        return Fail(error,L"A client directory ancestor changed identity or became unsafe",ancestor.path,ERROR_FILE_INVALID);
    }
    size_t observed=0;
    const auto check=[&](const fs::path &path) {
      const auto relative=path == root_ ? fs::path{} : path.lexically_relative(root_);
      Entry *entry=Find(relative);
      if(!entry || entry->removed) return Fail(error,L"A new client object appeared after confirmation",path,ERROR_FILE_INVALID);
      const bool directory=(entry->identity.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
      HANDLE file=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,
          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
          FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,nullptr);
      if(file == INVALID_HANDLE_VALUE) return Fail(error,L"A captured client object disappeared or became locked",path);
      BY_HANDLE_FILE_INFORMATION info{};
      const bool valid=Read(file,info,directory) && (directory ? SameObject(info,entry->identity) : SameFile(info,entry->identity));
      CloseHandle(file);
      if(!valid) return Fail(error,L"A captured client object was replaced or modified",path,ERROR_FILE_INVALID);
      ++observed;return true;
    };
    if(!check(root_)) return false;
    if(namespaceOnly_) return true;
    try { for(const auto &item:fs::recursive_directory_iterator(root_)) if(!check(item.path())) return false; }
    catch(const std::exception &) { return Fail(error,L"The client tree changed during verification",root_); }
    if(observed != static_cast<size_t>(std::count_if(entries_.begin(),entries_.end(),[](const Entry &entry){return !entry.removed;})))
      return Fail(error,L"A captured client object is missing",root_,ERROR_FILE_INVALID);
    return true;
  }
  bool Remove(const fs::path &path, std::wstring &error) {
    Entry *entry=Find(path == root_ ? fs::path{} : path.lexically_relative(root_));
    if(!entry || entry->removed) return Fail(error,L"An uncaptured object cannot be deleted",path,ERROR_FILE_INVALID);
    const bool directory=(entry->identity.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const bool reopenDirectory=directory && movable_ && !entry->relative.empty();
    // Keep the final metadata check and disposition in one interval that
    // excludes writers. Otherwise an external edit can land after SameFile
    // succeeds and be erased by the following disposition call.
    HANDLE handle=directory && !reopenDirectory ? entry->directory : CreateFileW(path.c_str(),DELETE | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(handle == INVALID_HANDLE_VALUE) return Fail(error,L"A captured client object could not be opened for deletion",path);
    BY_HANDLE_FILE_INFORMATION info{};
    if(!Read(handle,info,directory) || !(directory ? SameObject(info,entry->identity) : SameFile(info,entry->identity))) {
      if(!directory || reopenDirectory) CloseHandle(handle);
      return Fail(error,L"A captured client object changed before deletion",path,ERROR_FILE_INVALID);
    }
    FILE_DISPOSITION_INFO disposition{TRUE};
    const bool removed=SetFileInformationByHandle(handle,FileDispositionInfo,&disposition,sizeof(disposition)) != FALSE;
    const DWORD code=removed ? ERROR_SUCCESS : GetLastError();
    if(!directory || reopenDirectory || removed) CloseHandle(handle);
    if(!removed) return Fail(error,L"A captured client object could not be deleted",path,code);
    if(reopenDirectory && entry->directory != INVALID_HANDLE_VALUE) CloseHandle(entry->directory);
    entry->directory=INVALID_HANDLE_VALUE;entry->removed=true;return true;
  }
  bool MoveRoot(const fs::path &destination, std::wstring &error) {
    if(!UpgradeRoot(error)) return false;
    Entry *root=Find(fs::path{});
    if(!root || root->removed || root->directory == INVALID_HANDLE_VALUE) return false;
    if(!namespaceOnly_ && !movable_) {
      // Windows refuses a parent-directory rename while descendant directory
      // handles remain open, including read-attributes handles with delete
      // sharing. The payload is already removed at this point. Retire only
      // the surviving metadata directory handles, retaining their original
      // IDs and the exclusive root/ancestor guards. Cleanup reopens each
      // metadata item for DELETE and requires its original ID before acting.
      for(auto &entry:entries_) {
        if(entry.relative.empty() || entry.removed || entry.directory == INVALID_HANDLE_VALUE) continue;
        CloseHandle(entry.directory);entry.directory=INVALID_HANDLE_VALUE;
      }
      movable_=true;
#ifdef CTSPACES_CLIENT_DELETE_TEST_HOOKS
      BeforeMetadataMove(root_);
#endif
      if(!Verify(error)) return false;
    }
    const auto target=fs::absolute(destination).lexically_normal();
    if(!std::any_of(ancestors_.begin(),ancestors_.end(),[&](const Ancestor &entry) {
      return entry.handle != INVALID_HANDLE_VALUE && SamePath(entry.path,target.parent_path());
    })) return Fail(error,L"The client move destination parent is not pinned",target,ERROR_FILE_INVALID);
    const std::wstring name=target.native();
    const size_t bytes=name.size()*sizeof(wchar_t);
    const size_t size=offsetof(FILE_RENAME_INFO,FileName)+bytes+sizeof(wchar_t);
    if(size > MAXDWORD) return Fail(error,L"The deletion staging name is too long",target,ERROR_FILENAME_EXCED_RANGE);
    std::vector<unsigned char> storage(size);
    auto *rename=reinterpret_cast<FILE_RENAME_INFO *>(storage.data());
    rename->ReplaceIfExists=FALSE;rename->RootDirectory=nullptr;rename->FileNameLength=static_cast<DWORD>(bytes);
    memcpy(rename->FileName,name.data(),bytes);
#ifdef CTSPACES_CLIENT_NAMESPACE_TEST_HOOKS
    if(namespaceOnly_) BeforeNamespaceMove(root_,target,root->directory);
#endif
    for(unsigned attempt=0;attempt<8;++attempt) {
      if(!Verify(error)) return false;
      const DWORD attributes=GetFileAttributesW(target.c_str());
      if(attributes != INVALID_FILE_ATTRIBUTES) return Fail(error,L"The client move destination is occupied",target,ERROR_ALREADY_EXISTS);
      const DWORD absenceError=GetLastError();
      if(absenceError != ERROR_FILE_NOT_FOUND && absenceError != ERROR_PATH_NOT_FOUND)
        return Fail(error,L"The client move destination cannot be inspected safely",target,absenceError);
      if(SetFileInformationByHandle(root->directory,FileRenameInfo,rename,static_cast<DWORD>(size))) {
        root_=target;return true;
      }
      const DWORD moveError=GetLastError();
      // A just-vacated directory name may briefly return access denied. The
      // same original handle and every ancestor remain pinned during retry;
      // each attempt rechecks identity and the target's verified absence.
      if(moveError != ERROR_ACCESS_DENIED || attempt==7)
        return Fail(error,L"The captured client root could not be moved without replacement",target,moveError);
      Sleep(1u<<attempt);
    }
    return false;
  }
  bool IsAt(const fs::path &path) const { return SamePath(root_,fs::absolute(path).lexically_normal()); }
  bool RemoveRemaining(std::wstring &error) {
#ifdef CTSPACES_CLIENT_DELETE_TEST_HOOKS
    BeforeMetadataCleanup(root_);
#endif
    if(!Verify(error)) return false;
    std::vector<fs::path> remaining;
    for(const auto &entry:entries_) if(!entry.removed) remaining.push_back(entry.relative);
    std::sort(remaining.begin(),remaining.end(),[](const fs::path &a,const fs::path &b){return a.native().size() > b.native().size();});
    for(const auto &relative:remaining) if(!Remove(relative.empty() ? root_ : root_/relative,error)) return false;
    return true;
  }
  void RemoveEmptyPinnedAncestor(const fs::path &path) {
    const auto absolute=fs::absolute(path).lexically_normal();
    for(auto &sentinel:sentinels_) if(SamePath(absolute,sentinel.parent) && sentinel.handle != INVALID_HANDLE_VALUE) {
      CloseHandle(sentinel.handle);sentinel.handle=INVALID_HANDLE_VALUE;
    }
    for(auto &ancestor:ancestors_) if(SamePath(absolute,ancestor.path) && ancestor.handle != INVALID_HANDLE_VALUE) {
      // A DELETE-access destination-parent handle prevents Windows' own
      // directory rename. Pin parents with directory read access during staging;
      // afterward, re-open and require the exact original object for cleanup.
      CloseHandle(ancestor.handle);ancestor.handle=INVALID_HANDLE_VALUE;
      HANDLE cleanup=CreateFileW(absolute.c_str(),DELETE | FILE_READ_ATTRIBUTES,
          FILE_SHARE_READ | FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
          FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
      if(cleanup == INVALID_HANDLE_VALUE) return;
      BY_HANDLE_FILE_INFORMATION info{};
      FILE_DISPOSITION_INFO disposition{TRUE};
      if(Read(cleanup,info,true) && SameObject(info,ancestor.identity))
        SetFileInformationByHandle(cleanup,FileDispositionInfo,&disposition,sizeof(disposition));
      CloseHandle(cleanup);
      return;
    }
  }
};
}
