#pragma once

#include <windows.h>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

// Notes belong to the whole client container. Moving or backing up that
// container moves the notes with it; opening the editor never creates one.
namespace client_notes {
inline constexpr wchar_t kFileName[] = L"ctSpaces-client-notes.txt";
inline constexpr wchar_t kRichFileName[] = L"ctSpaces-client-notes.rtf";
inline constexpr wchar_t kNotebookFileName[] = L"ctSpaces-client-notes.ctn";
inline constexpr DWORD kMaxBytes = 32 * 1024 * 1024;
inline constexpr DWORD kMaxRichBytes = 64 * 1024 * 1024;
inline constexpr DWORD kMaxNotebookBytes = 256 * 1024 * 1024;
// Bound malformed input and whole-notebook saves without constraining ordinary
// client work to a few hundred notes. The byte limit remains independent.
inline constexpr size_t kMaxPages = 10000;
inline constexpr size_t kMaxLibraryPages = 256;
inline std::mutex mutex;
inline thread_local std::wstring persistenceError;
inline std::wstring PersistenceError() { return persistenceError; }
inline std::wstring PersistenceFailure(std::wstring message) {
  if(!persistenceError.empty()) message+=L"\n\n"+persistenceError;
  return message;
}
inline void RetainedRecovery(const std::filesystem::path &path) {
  persistenceError=L"A saved version was retained for recovery. Do not delete this folder:\n"+path.wstring().substr(0,2048);
}

enum class ReadStatus { Ok, Missing, Error };
struct ReadResult {
  ReadStatus status = ReadStatus::Error;
  std::wstring text;
};

inline bool SafeDirectory(const std::filesystem::path &root) {
  const DWORD attributes = GetFileAttributesW(root.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
         (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

struct DirectoryIdentity {
  DWORD volume=0,indexHigh=0,indexLow=0;
  bool valid=false;
  bool operator==(const DirectoryIdentity &) const = default;
};
// Keep every real path component in place while a notebook is read or saved.
// Omitting FILE_SHARE_DELETE blocks replacement of a client or its ancestors;
// identical notebook bytes alone do not establish client ownership.
struct NotebookDirectoryLease {
  std::vector<HANDLE> handles;
  DirectoryIdentity identity;
  NotebookDirectoryLease()=default;
  NotebookDirectoryLease(const NotebookDirectoryLease &)=delete;
  NotebookDirectoryLease &operator=(const NotebookDirectoryLease &)=delete;
  ~NotebookDirectoryLease() {for(HANDLE handle:handles) CloseHandle(handle);}
  void Release(){for(HANDLE handle:handles) CloseHandle(handle);handles.clear();}
  bool Acquire(const std::filesystem::path &root,bool allowRootWrites=false) {
    std::error_code error;const auto absolute=std::filesystem::absolute(root,error).lexically_normal();
    if(error || absolute.empty()) return false;
    auto current=absolute.root_path();
    const auto pin=[&] {
      const DWORD sharing=FILE_SHARE_READ | (current!=absolute || allowRootWrites ? FILE_SHARE_WRITE : 0);
      HANDLE handle=CreateFileW(current.c_str(),FILE_READ_ATTRIBUTES|FILE_LIST_DIRECTORY,sharing,nullptr,OPEN_EXISTING,
          FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
      if(handle==INVALID_HANDLE_VALUE) return false;
      BY_HANDLE_FILE_INFORMATION info{};
      if(!GetFileInformationByHandle(handle,&info) || !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) ||
          (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) {CloseHandle(handle);return false;}
      handles.push_back(handle);identity={info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow,true};return true;
    };
    if(!pin()) return false;
    for(const auto &component:absolute.relative_path()) {current/=component;if(!pin()) return false;}
    return true;
  }
  bool AllowWrites(const std::filesystem::path &root) {
    NotebookDirectoryLease writable;if(!writable.Acquire(root,true) || writable.identity!=identity) return false;
    Release();handles=std::move(writable.handles);writable.handles.clear();return true;
  }
};
struct NotebookWriteRoot {
  NotebookDirectoryLease lease;HANDLE sentinel=INVALID_HANDLE_VALUE;
  ~NotebookWriteRoot(){if(sentinel!=INVALID_HANDLE_VALUE) CloseHandle(sentinel);}
  bool Acquire(const std::filesystem::path &root) {
    if(!lease.Acquire(root)) return false;
    for(unsigned i=0;i<16;++i) {
      const auto path=root/(L".ctSpaces-guard-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(i));
      sentinel=CreateFileW(path.c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
      if(sentinel!=INVALID_HANDLE_VALUE) break;
      if(GetLastError()!=ERROR_FILE_EXISTS && GetLastError()!=ERROR_ALREADY_EXISTS) return false;
    }
    return sentinel!=INVALID_HANDLE_VALUE && lease.AllowWrites(root);
  }
};
struct NotebookTransaction {
  HANDLE handle=nullptr;bool owned=false;
  NotebookTransaction()=default;NotebookTransaction(const NotebookTransaction &)=delete;NotebookTransaction &operator=(const NotebookTransaction &)=delete;
  ~NotebookTransaction(){if(owned) ReleaseMutex(handle);if(handle) CloseHandle(handle);}
  bool Acquire(const DirectoryIdentity &identity) {
    if(!identity.valid) return false;
    const auto name=L"Local\\ctSpaces.Notebook."+std::to_wstring(identity.volume)+L"."+std::to_wstring(identity.indexHigh)+L"."+std::to_wstring(identity.indexLow);
    handle=CreateMutexW(nullptr,FALSE,name.c_str());if(!handle) return false;
    const auto wait=WaitForSingleObject(handle,10000);owned=wait==WAIT_OBJECT_0 || wait==WAIT_ABANDONED;return owned;
  }
};

inline bool SafeFileHandle(HANDLE file, DWORD &size, DWORD maxBytes = kMaxBytes) {
  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(file, &info) ||
      (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY |
                                FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
      info.nNumberOfLinks != 1 || info.nFileSizeHigh != 0 ||
      info.nFileSizeLow > maxBytes || GetFileType(file) != FILE_TYPE_DISK)
    return false;
  size = info.nFileSizeLow;
  return true;
}

inline bool DecodeUtf8(const std::string &bytes, std::wstring &text) {
  text.clear();
  if (bytes.empty())
    return true;
  if (bytes.find('\0') != std::string::npos)
    return false;
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         bytes.data(),
                                         static_cast<int>(bytes.size()),
                                         nullptr, 0);
  if (length <= 0)
    return false;
  text.resize(length);
  return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
                             static_cast<int>(bytes.size()), text.data(),
                             length) == length;
}

inline bool EncodeUtf8(std::wstring_view text, std::string &bytes) {
  bytes.clear();
  if (text.empty())
    return true;
  if (text.find(L'\0') != std::wstring_view::npos)
    return false;
  if (text.size() > kMaxBytes)
    return false;
  const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                         text.data(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
  if (length <= 0 || length > static_cast<int>(kMaxBytes))
    return false;
  bytes.resize(length);
  return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                             static_cast<int>(text.size()), bytes.data(),
                             length, nullptr, nullptr) == length;
}

inline ReadResult Read(const std::filesystem::path &root) {
  if (!SafeDirectory(root))
    return {};
  const auto path = root / kFileName;
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    return {error == ERROR_FILE_NOT_FOUND ? ReadStatus::Missing
                                         : ReadStatus::Error, {}};
  }
  DWORD size = 0;
  std::string bytes;
  bool safe = SafeFileHandle(file, size);
  if (safe) {
    bytes.resize(size);
    DWORD actual = 0;
    safe = size == 0 || (ReadFile(file, bytes.data(), size, &actual, nullptr) &&
                         actual == size);
  }
  safe = CloseHandle(file) != FALSE && safe;
  std::wstring text;
  if (!safe || !SafeDirectory(root) || !DecodeUtf8(bytes, text))
    return {};
  return {ReadStatus::Ok, std::move(text)};
}

// The expected snapshot prevents a stale editor from replacing notes changed
// by another process. A failed read is never treated as an empty note.
inline bool Write(const std::filesystem::path &root, std::wstring_view text,
                  const ReadResult &expected,
                  const std::function<bool()> &revalidate = {}) {
  std::lock_guard<std::mutex> lock(mutex);
  std::string bytes;
  if (!SafeDirectory(root) || !EncodeUtf8(text, bytes) ||
      (revalidate && !revalidate()))
    return false;
  const ReadResult current = Read(root);
  if (current.status == ReadStatus::Error ||
      current.status != expected.status || current.text != expected.text)
    return false;

  const auto path = root / kFileName;
  for (unsigned attempt = 0; attempt < 16; ++attempt) {
    const auto stage = root /
        (L".ctSpaces-notes-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt));
    HANDLE file = CreateFileW(stage.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL |
                                              FILE_FLAG_WRITE_THROUGH,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      if (GetLastError() == ERROR_FILE_EXISTS ||
          GetLastError() == ERROR_ALREADY_EXISTS)
        continue;
      return false;
    }
    DWORD written = 0;
    const bool saved = (bytes.empty() ||
                        (WriteFile(file, bytes.data(),
                                   static_cast<DWORD>(bytes.size()), &written,
                                   nullptr) && written == bytes.size())) &&
                       FlushFileBuffers(file) != FALSE;
    const bool closed = CloseHandle(file) != FALSE;
    const bool ready = saved && closed && SafeDirectory(root) &&
                       (!revalidate || revalidate()) &&
                       [&]() {
                         const ReadResult latest = Read(root);
                         return latest.status == expected.status &&
                                latest.text == expected.text;
                       }();
    const bool moved = ready &&
        MoveFileExW(stage.c_str(), path.c_str(),
                    MOVEFILE_WRITE_THROUGH |
                    (expected.status == ReadStatus::Missing
                         ? 0 : MOVEFILE_REPLACE_EXISTING));
    if (!moved) {
      DeleteFileW(stage.c_str());
      return false;
    }
    return true;
  }
  return false;
}

// A document snapshot includes both formats. In particular, a legacy TXT
// change must invalidate an editor which is about to save its first RTF file.
struct FileSnapshot {
  ReadStatus status = ReadStatus::Error;
  std::string bytes;
};

struct DocumentResult {
  ReadStatus status = ReadStatus::Error;
  bool rich = false;
  std::string rtf;
  std::wstring text;
  FileSnapshot plain_snapshot;
  FileSnapshot rich_snapshot;
};

enum class RecoveryOwner { Unknown, Matching, Different };
inline RecoveryOwner RecoveryOwnerFor(const std::filesystem::path &recovery,const wchar_t *name) {
  if(!SafeDirectory(recovery)) return RecoveryOwner::Unknown;
  HANDLE file=CreateFileW((recovery/L"target").c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
      nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(file==INVALID_HANDLE_VALUE) return RecoveryOwner::Unknown;
  DWORD size=0,actual=0;std::string bytes;
  bool ok=SafeFileHandle(file,size,2048) && size>8;
  if(ok){bytes.resize(size);ok=ReadFile(file,bytes.data(),size,&actual,nullptr)!=FALSE && actual==size;}
  CloseHandle(file);std::wstring target;
  if(!ok || bytes.substr(0,8)!="CTNSAVE1" || !DecodeUtf8(bytes.substr(8),target) || target.empty() || target.size()>255 || target.find_first_of(L"/\\:")!=std::wstring::npos) return RecoveryOwner::Unknown;
  return CompareStringOrdinal(target.c_str(),-1,name,-1,TRUE)==CSTR_EQUAL ? RecoveryOwner::Matching : RecoveryOwner::Different;
}
inline bool RecoveryHasSavedVersion(const std::filesystem::path &recovery) {
  for(const auto *name:{L"previous",L"rollback"}) {
    if(GetFileAttributesW((recovery/name).c_str())!=INVALID_FILE_ATTRIBUTES || GetLastError()!=ERROR_FILE_NOT_FOUND) return true;
  }
  return false;
}
inline FileSnapshot ReadFileSnapshot(const std::filesystem::path &root,
                                     const wchar_t *name, DWORD maxBytes,bool checkRecovery=true) {
  if (!SafeDirectory(root))
    return {};
  const auto path = root / name;
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    if(GetLastError()!=ERROR_FILE_NOT_FOUND) return {};
    // A partial native publication/crash must never look like a fresh empty
    // library or notebook while complete displaced versions remain here.
    std::error_code error;
    if(checkRecovery) for(std::filesystem::directory_iterator it(root,error),end;!error && it!=end;it.increment(error))
      if(it->path().filename().wstring().starts_with(L".ctSpaces-save-")) {
        const auto owner=RecoveryOwnerFor(it->path(),name);
        if(owner==RecoveryOwner::Matching || (owner==RecoveryOwner::Unknown && RecoveryHasSavedVersion(it->path()))) {RetainedRecovery(it->path());return {};}
      }
    return {error ? ReadStatus::Error : ReadStatus::Missing,{}};
  }
  DWORD size = 0;
  std::string bytes;
  bool safe = SafeFileHandle(file, size, maxBytes);
  if (safe) {
    bytes.resize(size);
    DWORD actual = 0;
    safe = size == 0 || (ReadFile(file, bytes.data(), size, &actual, nullptr) &&
                         actual == size);
  }
  safe = CloseHandle(file) != FALSE && safe;
  if (!safe || !SafeDirectory(root))
    return {};
  return {ReadStatus::Ok, std::move(bytes)};
}

// Rich Edit's text-only RTF export is a small subset of RTF. Verify the
// structure before handing it to Rich Edit, and reject embedded objects,
// pictures, fields, external file references, and binary payloads.
inline bool ValidRichText(std::string_view rtf) {
  if (rtf.size() < 7 || rtf.size() > kMaxRichBytes ||
      rtf.substr(0, 6) != "{\\rtf1" ||
      rtf.find('\0') != std::string_view::npos)
    return false;
  unsigned depth = 0;
  bool ended = false;
  for (size_t i = 0; i < rtf.size();) {
    const unsigned char ch = static_cast<unsigned char>(rtf[i]);
    if (ended) {
      if (ch != ' ' && ch != '\r' && ch != '\n' && ch != '\t')
        return false;
      ++i;
    } else if (ch == '{') {
      if (++depth > 64)
        return false;
      ++i;
    } else if (ch == '}') {
      if (depth == 0)
        return false;
      ended = --depth == 0;
      ++i;
    } else if (ch == '\\') {
      if (++i == rtf.size())
        return false;
      const unsigned char next = static_cast<unsigned char>(rtf[i]);
      if (std::isalpha(next)) {
        const size_t start = i;
        while (i < rtf.size() &&
               std::isalpha(static_cast<unsigned char>(rtf[i])))
          ++i;
        std::string word(rtf.substr(start, i - start));
        for (char &letter : word)
          letter = static_cast<char>(
              std::tolower(static_cast<unsigned char>(letter)));
        if (word.size() > 64 || word == "bin" || word == "object" ||
            word == "pict" || word == "field" || word == "fldinst" ||
            word == "fldrslt" || word == "datafield" || word == "file" ||
            word == "filetbl" || word == "filename" ||
            word == "includegraphics" || word == "htmltag" ||
            word == "htmlrtf" || word == "do" ||
            word.starts_with("obj") || word.starts_with("shp") ||
            word.starts_with("blip"))
          return false;
        if (i < rtf.size() && rtf[i] == '-')
          ++i;
        while (i < rtf.size() &&
               std::isdigit(static_cast<unsigned char>(rtf[i])))
          ++i;
        if (i < rtf.size() && rtf[i] == ' ')
          ++i;
      } else if (next == '\'') {
        if (i + 2 >= rtf.size() ||
            !std::isxdigit(static_cast<unsigned char>(rtf[i + 1])) ||
            !std::isxdigit(static_cast<unsigned char>(rtf[i + 2])))
          return false;
        i += 3;
      } else if (next == '\\' || next == '{' || next == '}' ||
                 next == '~' || next == '-' || next == '_' || next == '*') {
        ++i;
      } else {
        return false;
      }
    } else {
      if (ch < 0x20 && ch != '\r' && ch != '\n' && ch != '\t')
        return false;
      ++i;
    }
  }
  return ended && depth == 0;
}

inline DocumentResult ReadDocument(const std::filesystem::path &root) {
  DocumentResult result;
  result.plain_snapshot = ReadFileSnapshot(root, kFileName, kMaxBytes);
  result.rich_snapshot = ReadFileSnapshot(root, kRichFileName, kMaxRichBytes);
  if (result.plain_snapshot.status == ReadStatus::Error ||
      result.rich_snapshot.status == ReadStatus::Error)
    return result;
  if (result.rich_snapshot.status == ReadStatus::Ok) {
    if (!ValidRichText(result.rich_snapshot.bytes))
      return result;
    result.status = ReadStatus::Ok;
    result.rich = true;
    result.rtf = result.rich_snapshot.bytes;
    return result;
  }
  if (result.plain_snapshot.status == ReadStatus::Missing) {
    result.status = ReadStatus::Missing;
    return result;
  }
  if (DecodeUtf8(result.plain_snapshot.bytes, result.text))
    result.status = ReadStatus::Ok;
  return result;
}

// RichEdit exports U+2003 as \emspace but imports that control as U+0020.
// Use an explicit Unicode group so checklist spacing survives a round trip.
// Tokenize escaped backslashes too: literal text such as "\\emspace" is data.
inline bool PreserveEditorSpacing(std::string &rtf) {
  std::string normalized;
  normalized.reserve(rtf.size());
  for (size_t i = 0; i < rtf.size();) {
    if (rtf[i] != '\\' || i + 1 == rtf.size()) {
      normalized.push_back(rtf[i++]);
    } else {
      const size_t start = i++;
      if (!std::isalpha(static_cast<unsigned char>(rtf[i]))) {
        normalized.append(rtf, start, 2);
        ++i;
      } else {
        const size_t word = i;
        while (i < rtf.size() && std::isalpha(static_cast<unsigned char>(rtf[i]))) ++i;
        if (rtf.compare(word, i - word, "emspace") == 0 &&
            (i == rtf.size() || (rtf[i] != '-' && !std::isdigit(static_cast<unsigned char>(rtf[i]))))) {
          if (i < rtf.size() && rtf[i] == ' ') ++i;
          normalized += "{\\uc1\\u8195?}";
        } else {
          normalized.append(rtf, start, i - start);
        }
      }
    }
    if (normalized.size() > kMaxRichBytes) return false;
  }
  rtf = std::move(normalized);
  return true;
}

// Use only for a native editor export. Store hyperlink display text without
// its field instruction; RichEdit detects the URL again on the next load.
// Incoming files must still pass ValidRichText without this normalization.
inline bool NormalizeEditorExport(std::string &rtf) {
  if (!rtf.empty() && rtf.back() == '\0')
    rtf.pop_back();
  const auto groupEnd = [&](size_t start) -> size_t {
    unsigned depth = 0;
    for (size_t i = start; i < rtf.size(); ++i) {
      if (rtf[i] == '\\') {
        if (++i == rtf.size()) return std::string::npos;
      } else if (rtf[i] == '{') {
        if (++depth > 64) return std::string::npos;
      } else if (rtf[i] == '}') {
        if (!depth) return std::string::npos;
        if (--depth == 0) return i + 1;
      }
    }
    return std::string::npos;
  };
  size_t cursor = 0;
  while ((cursor = rtf.find("{\\field", cursor)) != std::string::npos) {
    const size_t fieldEnd = groupEnd(cursor);
    const size_t instruction = cursor + 7;
    if (fieldEnd == std::string::npos ||
        rtf.compare(instruction, 11, "{\\*\\fldinst") != 0)
      return false;
    const size_t instructionEnd = groupEnd(instruction);
    if (instructionEnd == std::string::npos || instructionEnd >= fieldEnd ||
        rtf.compare(instructionEnd, 9, "{\\fldrslt") != 0)
      return false;
    const size_t resultEnd = groupEnd(instructionEnd);
    if (resultEnd != fieldEnd - 1)
      return false;
    const size_t content = instructionEnd + 9;
    std::string display = "{" + rtf.substr(content, resultEnd - 1 - content) + "}";
    rtf.replace(cursor, fieldEnd - cursor, display);
    // Examine the inserted display as well; any nested field must be flattened.
  }
  return PreserveEditorSpacing(rtf) && ValidRichText(rtf);
}

inline bool SameSnapshot(const FileSnapshot &a, const FileSnapshot &b) {
  return a.status == b.status && a.bytes == b.bytes;
}

inline bool WriteDocument(const std::filesystem::path &root,
                          std::string_view rtf, const DocumentResult &expected,
                          const std::function<bool()> &revalidate = {}) {
  std::lock_guard<std::mutex> lock(mutex);
  if (!SafeDirectory(root) || !ValidRichText(rtf) ||
      expected.status == ReadStatus::Error ||
      (revalidate && !revalidate()))
    return false;
  const auto matches = [&]() {
    const DocumentResult current = ReadDocument(root);
    return current.status != ReadStatus::Error &&
           SameSnapshot(current.plain_snapshot, expected.plain_snapshot) &&
           SameSnapshot(current.rich_snapshot, expected.rich_snapshot);
  };
  if (!matches())
    return false;

  const auto path = root / kRichFileName;
  for (unsigned attempt = 0; attempt < 16; ++attempt) {
    const auto stage = root /
        (L".ctSpaces-rich-notes-" + std::to_wstring(GetCurrentProcessId()) +
         L"-" + std::to_wstring(GetTickCount64()) + L"-" +
         std::to_wstring(attempt));
    HANDLE file = CreateFileW(stage.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL |
                                              FILE_FLAG_WRITE_THROUGH,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      if (GetLastError() == ERROR_FILE_EXISTS ||
          GetLastError() == ERROR_ALREADY_EXISTS)
        continue;
      return false;
    }
    DWORD written = 0;
    const bool saved = WriteFile(file, rtf.data(),
                                 static_cast<DWORD>(rtf.size()), &written,
                                 nullptr) && written == rtf.size() &&
                       FlushFileBuffers(file) != FALSE;
    const bool closed = CloseHandle(file) != FALSE;
    const bool ready = saved && closed && SafeDirectory(root) &&
                       (!revalidate || revalidate()) && matches();
    const bool moved = ready &&
        MoveFileExW(stage.c_str(), path.c_str(),
                    MOVEFILE_WRITE_THROUGH |
                    (expected.rich_snapshot.status == ReadStatus::Missing
                         ? 0 : MOVEFILE_REPLACE_EXISTING));
    if (!moved) {
      DeleteFileW(stage.c_str());
      return false;
    }
    return true;
  }
  return false;
}

inline constexpr size_t kMaxLinkRanges=16384;
struct LinkRange {
  uint32_t first=0,last=0,target=0;
  bool operator==(const LinkRange &) const = default;
};
struct Revision {
  uint64_t time = 0;
  std::string rtf;
  std::vector<std::pair<std::wstring,std::wstring>> links;
  std::vector<LinkRange> linkRanges;
  bool linkRangesKnown=false;
};
inline uint64_t Now() {
  FILETIME time{}; GetSystemTimeAsFileTime(&time);
  return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}
struct NotePage {
  std::wstring name;
  bool rich = false; // False is used only for an unsaved legacy TXT page.
  std::string rtf;
  std::wstring text;
  uint64_t id = 0, created = 0, modified = 0;
  bool open = true, archived = false, deleted = false, pinned = false, active = false;
  bool pristine = false;
  uint32_t cursor = 0, scroll = 0, zoom = 100;
  std::wstring ticket, ticketUrl;
  std::vector<std::pair<std::wstring,std::wstring>> links;
  std::vector<Revision> history;
  uint32_t status = 0; // 0 none, 1 investigating, 2 waiting, 3 follow-up, 4 resolved.
  std::vector<std::wstring> tags;
  std::vector<LinkRange> linkRanges;
  bool linkRangesKnown=false;
};

inline uint64_t NextId(const std::vector<NotePage> &pages) {
  uint64_t id = Now();
  if (!id) id=1;
  while (std::any_of(pages.begin(),pages.end(),[&](const auto &p){return p.id==id;}))
    if (++id==0) id=1;
  return id;
}
inline size_t RevisionBytes(const Revision &revision) {
  size_t bytes=revision.rtf.size()+revision.linkRanges.size()*sizeof(LinkRange);
  for(const auto &[label,url]:revision.links) bytes+=(label.size()+url.size())*sizeof(wchar_t);
  return bytes;
}
inline void RememberRevision(NotePage &page, const std::string &previous, bool force = false,
                             const std::vector<std::pair<std::wstring,std::wstring>> *previousLinks = nullptr,
                             const std::vector<LinkRange> *previousRanges=nullptr,bool previousRangesKnown=false) {
  const uint64_t now = Now();
  const auto &links=previousLinks ? *previousLinks : page.links;
  const auto &ranges=previousRanges ? *previousRanges : page.linkRanges;
  const bool known=previousLinks ? previousRangesKnown : page.linkRangesKnown;
  if (previous.empty() || (previous == page.rtf && links==page.links && ranges==page.linkRanges && known==page.linkRangesKnown) || !ValidRichText(previous)) return;
  if (!page.history.empty() && ((page.history.back().rtf == previous && page.history.back().links==links && page.history.back().linkRanges==ranges && page.history.back().linkRangesKnown==known) ||
      (!force && links==page.links && now - page.history.back().time < 60ULL * 10000000))) return;
  page.history.push_back({now, previous,links,ranges,known});
  size_t bytes = 0; for (const auto &item : page.history) bytes += RevisionBytes(item);
  while (page.history.size() > 20 || bytes > 32 * 1024 * 1024) {
    bytes -= RevisionBytes(page.history.front()); page.history.erase(page.history.begin());
  }
}

struct NotebookResult {
  ReadStatus status = ReadStatus::Error;
  std::vector<NotePage> pages;
  FileSnapshot plain_snapshot;
  FileSnapshot rich_snapshot;
  FileSnapshot notebook_snapshot;
  DirectoryIdentity directory_identity;
};

inline bool NameSpace(wchar_t ch) {
  return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n' ||
         ch == 0x00a0 || ch == 0x1680 ||
         (ch >= 0x2000 && ch <= 0x200a) || ch == 0x2028 ||
         ch == 0x2029 || ch == 0x202f || ch == 0x205f ||
         ch == 0x3000;
}

inline bool ValidPageName(std::wstring_view name) {
  if (name.empty() || name.size() > 64 || NameSpace(name.front()) ||
      NameSpace(name.back()))
    return false;
  for (size_t i = 0; i < name.size(); ++i) {
    const wchar_t ch = name[i];
    if (ch < 0x20 || (ch >= 0x7f && ch <= 0x9f) ||
        ch == 0x00ad || ch == 0x061c || ch == 0x180e ||
        (ch >= 0x200b && ch <= 0x200f) ||
        (ch >= 0x2028 && ch <= 0x202e) ||
        (ch >= 0x2060 && ch <= 0x206f) || ch == 0xfeff ||
        (ch >= 0xfdd0 && ch <= 0xfdef) ||
        (ch >= 0xfff9 && ch <= 0xffff))
      return false;
    if (ch >= 0xd800 && ch <= 0xdbff) {
      if (++i == name.size() || name[i] < 0xdc00 || name[i] > 0xdfff)
        return false;
      const uint32_t codepoint = 0x10000 +
          (static_cast<uint32_t>(ch) - 0xd800) * 0x400 +
          (static_cast<uint32_t>(name[i]) - 0xdc00);
      if ((codepoint & 0xfffeu) == 0xfffeu)
        return false;
    } else if (ch >= 0xdc00 && ch <= 0xdfff) {
      return false;
    }
  }
  std::string encoded;
  return EncodeUtf8(name, encoded);
}

inline bool SamePageName(std::wstring_view a, std::wstring_view b) {
  return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                              static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

inline void AppendU32(std::string &bytes, uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8)
    bytes.push_back(static_cast<char>(value >> shift));
}

inline bool ReadU32(std::string_view bytes, size_t &offset, uint32_t &value) {
  if (offset > bytes.size() || bytes.size() - offset < 4)
    return false;
  value = 0;
  for (unsigned shift = 0; shift < 32; shift += 8)
    value |= static_cast<uint32_t>(
                 static_cast<unsigned char>(bytes[offset++])) << shift;
  return true;
}

inline void AppendU64(std::string &bytes, uint64_t n) {
  AppendU32(bytes, static_cast<uint32_t>(n)); AppendU32(bytes, static_cast<uint32_t>(n >> 32));
}
inline bool ReadU64(std::string_view bytes, size_t &pos, uint64_t &n) {
  uint32_t lo, hi;
  if (!ReadU32(bytes, pos, lo) || !ReadU32(bytes, pos, hi)) return false;
  n = lo | (static_cast<uint64_t>(hi) << 32); return true;
}
inline bool AppendText(std::string &bytes, const std::wstring &value, size_t limit) {
  std::string utf8;
  if (value.size() > limit || !EncodeUtf8(value, utf8)) return false;
  AppendU32(bytes, static_cast<uint32_t>(utf8.size())); bytes += utf8; return true;
}
inline bool ReadText(std::string_view bytes, size_t &pos, std::wstring &value, size_t limit) {
  uint32_t size;
  if (!ReadU32(bytes, pos, size) || size > limit * 4 || size > bytes.size() - pos) return false;
  const bool ok = DecodeUtf8(std::string(bytes.substr(pos, size)), value) && value.size() <= limit;
  pos += size; return ok;
}
inline bool ValidLinkRanges(const std::vector<LinkRange> &ranges,bool known,size_t targets) {
  if(ranges.size()>kMaxLinkRanges || (!known && !ranges.empty())) return false;
  if(targets>256) return false;
  std::vector<bool> used(targets,false);
  uint32_t end=0;
  for(const auto &range:ranges) {
    if(range.first<end || range.first>=range.last || range.last>kMaxRichBytes || range.target>=targets) return false;
    end=range.last;
    used[range.target]=true;
  }
  return !known || std::all_of(used.begin(),used.end(),[](bool value){return value;});
}
inline bool AppendLinkRanges(std::string &bytes,const std::vector<LinkRange> &ranges,bool known,size_t targets) {
  if(!ValidLinkRanges(ranges,known,targets)) return false;
  AppendU32(bytes,known?1:0);AppendU32(bytes,static_cast<uint32_t>(ranges.size()));
  for(const auto &range:ranges) {AppendU32(bytes,range.first);AppendU32(bytes,range.last);AppendU32(bytes,range.target);}return true;
}
inline bool ReadLinkRanges(std::string_view bytes,size_t &pos,std::vector<LinkRange> &ranges,bool &known,size_t targets) {
  uint32_t flag,count;if(!ReadU32(bytes,pos,flag) || flag>1 || !ReadU32(bytes,pos,count) || count>kMaxLinkRanges || count>(bytes.size()-pos)/12) return false;
  known=flag!=0;
  for(uint32_t i=0;i<count;++i) {LinkRange range;if(!ReadU32(bytes,pos,range.first) || !ReadU32(bytes,pos,range.last) || !ReadU32(bytes,pos,range.target)) return false;ranges.push_back(range);}
  return ValidLinkRanges(ranges,known,targets);
}

// Version 2 adds stable identities, lifecycle, view state and bounded revisions.
// Version 3 adds work status and tags only when used. V1/V2 and legacy TXT/RTF
// remain readable; older releases reject newer formats without overwriting them.
// Version 4 stores historical friendly-link destinations only when a revision
// needs them. Older revisions have no URL metadata and never borrow current URLs.
// Version 5 records exact native UTF-16 occurrences, so a repeated caption's
// ordinary underline cannot acquire another occurrence's URL on reopening.
inline bool SerializeNotebook(const std::vector<NotePage> &pages, std::string &bytes) {
  bytes = "CTNBOOK1";
  if (pages.size() > kMaxPages) return false;
  const bool metadata=std::any_of(pages.begin(),pages.end(),[](const auto &p){return p.status || !p.tags.empty();});
  const bool revisionLinks=std::any_of(pages.begin(),pages.end(),[](const auto &p){return std::any_of(p.history.begin(),p.history.end(),[](const auto &r){return !r.links.empty();});});
  const bool occurrenceLinks=std::any_of(pages.begin(),pages.end(),[](const auto &p){return p.linkRangesKnown || std::any_of(p.history.begin(),p.history.end(),[](const auto &r){return r.linkRangesKnown;});});
  AppendU32(bytes, occurrenceLinks ? 5 : revisionLinks ? 4 : metadata ? 3 : 2); AppendU32(bytes, static_cast<uint32_t>(pages.size()));
  const auto byName=[](const std::wstring &a,const std::wstring &b) {
    return CompareStringOrdinal(a.data(),static_cast<int>(a.size()),b.data(),static_cast<int>(b.size()),TRUE)==CSTR_LESS_THAN;
  };
  std::set<std::wstring,decltype(byName)> names(byName);
  std::set<uint64_t> identities;
  for (size_t i = 0; i < pages.size(); ++i) {
    const auto &p = pages[i]; std::string name;
    if (!p.rich || !p.text.empty() || !ValidPageName(p.name) || !EncodeUtf8(p.name, name) ||
        !ValidRichText(p.rtf) || p.history.size() > 20 || p.zoom < 25 || p.zoom > 400) return false;
    const uint64_t id = p.id ? p.id : i + 1;
    if (!names.insert(p.name).second || !identities.insert(id).second) return false;
    AppendU32(bytes, static_cast<uint32_t>(name.size()));
    AppendU32(bytes, static_cast<uint32_t>(p.rtf.size())); bytes += name; bytes += p.rtf;
    AppendU64(bytes, id); AppendU64(bytes, p.created); AppendU64(bytes, p.modified);
    AppendU32(bytes, (p.open ? 1 : 0) | (p.archived ? 2 : 0) | (p.deleted ? 4 : 0) |
        (p.pinned ? 8 : 0) | (p.active ? 16 : 0) | (p.pristine ? 32 : 0));
    AppendU32(bytes, p.cursor); AppendU32(bytes, p.scroll); AppendU32(bytes, p.zoom);
    if (!AppendText(bytes, p.ticket, 128) || !AppendText(bytes, p.ticketUrl, 2048)) return false;
    if (p.links.size() > 256) return false;
    AppendU32(bytes,static_cast<uint32_t>(p.links.size()));
    for (const auto &[label,url] : p.links)
      if (!AppendText(bytes,label,2048) || !AppendText(bytes,url,2048)) return false;
    if(!ValidLinkRanges(p.linkRanges,p.linkRangesKnown,p.links.size()) || (occurrenceLinks && !AppendLinkRanges(bytes,p.linkRanges,p.linkRangesKnown,p.links.size()))) return false;
    AppendU32(bytes, static_cast<uint32_t>(p.history.size()));
    size_t historyBytes = 0;
    for (const auto &rev : p.history) {
      if (!ValidRichText(rev.rtf) || (historyBytes += RevisionBytes(rev)) > 32 * 1024 * 1024 || rev.links.size()>256) return false;
      AppendU64(bytes, rev.time); AppendU32(bytes, static_cast<uint32_t>(rev.rtf.size())); bytes += rev.rtf;
      if(revisionLinks || occurrenceLinks) {
        AppendU32(bytes,static_cast<uint32_t>(rev.links.size()));
        for(const auto &[label,url]:rev.links)
          if(!AppendText(bytes,label,2048) || !AppendText(bytes,url,2048)) return false;
      }
      if(!ValidLinkRanges(rev.linkRanges,rev.linkRangesKnown,rev.links.size()) || (occurrenceLinks && !AppendLinkRanges(bytes,rev.linkRanges,rev.linkRangesKnown,rev.links.size()))) return false;
    }
    if (metadata || revisionLinks || occurrenceLinks) {
      if (p.status>4 || p.tags.size()>16) return false;
      AppendU32(bytes,p.status); AppendU32(bytes,static_cast<uint32_t>(p.tags.size()));
      for (size_t j=0;j<p.tags.size();++j) {
        if (p.tags[j].size()>32 || !ValidPageName(p.tags[j]) || p.tags[j].find(L',')!=std::wstring::npos || !AppendText(bytes,p.tags[j],32)) return false;
        for (size_t k=0;k<j;++k) if (SamePageName(p.tags[j],p.tags[k])) return false;
      }
    }
    if (bytes.size() > kMaxNotebookBytes) return false;
  }
  return true;
}
inline bool ParseNotebook(std::string_view bytes, std::vector<NotePage> &pages) {
  pages.clear();
  if (bytes.size() < 16 || bytes.size() > kMaxNotebookBytes || bytes.substr(0,8) != "CTNBOOK1") return false;
  size_t pos = 8; uint32_t version, count;
  if (!ReadU32(bytes,pos,version) || version<1 || version>5 || !ReadU32(bytes,pos,count) ||
      count > kMaxPages || (!count && version == 1)) return false;
  std::vector<NotePage> parsed;
  const auto byName=[](const std::wstring &a,const std::wstring &b) {
    return CompareStringOrdinal(a.data(),static_cast<int>(a.size()),b.data(),static_cast<int>(b.size()),TRUE)==CSTR_LESS_THAN;
  };
  std::set<std::wstring,decltype(byName)> names(byName);
  std::set<uint64_t> identities;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t n, r;
    if (!ReadU32(bytes,pos,n) || !ReadU32(bytes,pos,r) || !n || n > 256 || r > kMaxRichBytes ||
        static_cast<size_t>(n) + r > bytes.size() - pos) return false;
    NotePage p;
    if (!DecodeUtf8(std::string(bytes.substr(pos,n)),p.name) || !ValidPageName(p.name)) return false;
    pos += n; p.rtf = bytes.substr(pos,r); pos += r; p.rich = true;
    if (!ValidRichText(p.rtf)) return false;
    p.id = i + 1;
    if (version >= 2) {
      uint32_t flags, revisions;
      if (!ReadU64(bytes,pos,p.id) || !p.id || !ReadU64(bytes,pos,p.created) || !ReadU64(bytes,pos,p.modified) ||
          !ReadU32(bytes,pos,flags) || flags > 63 || !ReadU32(bytes,pos,p.cursor) ||
          !ReadU32(bytes,pos,p.scroll) || !ReadU32(bytes,pos,p.zoom) || p.zoom < 25 || p.zoom > 400 ||
          !ReadText(bytes,pos,p.ticket,128) || !ReadText(bytes,pos,p.ticketUrl,2048)) return false;
      uint32_t linkCount;
      if (!ReadU32(bytes,pos,linkCount) || linkCount > 256) return false;
      for (uint32_t j=0;j<linkCount;++j) {
        std::wstring label,url;
        if (!ReadText(bytes,pos,label,2048) || !ReadText(bytes,pos,url,2048)) return false;
        p.links.emplace_back(std::move(label),std::move(url));
      }
      if(version>=5 && !ReadLinkRanges(bytes,pos,p.linkRanges,p.linkRangesKnown,p.links.size())) return false;
      if (!ReadU32(bytes,pos,revisions) || revisions > 20) return false;
      p.open = (flags & 1) != 0; p.archived = (flags & 2) != 0; p.deleted = (flags & 4) != 0;
      p.pinned = (flags & 8) != 0; p.active = (flags & 16) != 0; p.pristine = (flags & 32) != 0;
      size_t historyBytes = 0;
      for (uint32_t j = 0; j < revisions; ++j) {
        Revision rev;
        if (!ReadU64(bytes,pos,rev.time) || !ReadU32(bytes,pos,r) || r > bytes.size() - pos ||
            (historyBytes += r) > 32 * 1024 * 1024) return false;
        rev.rtf = bytes.substr(pos,r); pos += r;
        if (!ValidRichText(rev.rtf)) return false;
        if(version>=4) {
          uint32_t links;if(!ReadU32(bytes,pos,links) || links>256) return false;
          for(uint32_t k=0;k<links;++k) {
            std::wstring label,url;if(!ReadText(bytes,pos,label,2048) || !ReadText(bytes,pos,url,2048)) return false;
            historyBytes+=(label.size()+url.size())*sizeof(wchar_t);
            if(historyBytes>32*1024*1024) return false;
            rev.links.emplace_back(std::move(label),std::move(url));
          }
        }
        if(version>=5) {
          if(!ReadLinkRanges(bytes,pos,rev.linkRanges,rev.linkRangesKnown,rev.links.size())) return false;
          historyBytes+=rev.linkRanges.size()*sizeof(LinkRange);if(historyBytes>32*1024*1024) return false;
        }
        p.history.push_back(std::move(rev));
      }
    }
    if (version>=3) {
      uint32_t tagCount;
      if (!ReadU32(bytes,pos,p.status) || p.status>4 || !ReadU32(bytes,pos,tagCount) || tagCount>16) return false;
      for (uint32_t j=0;j<tagCount;++j) {
        std::wstring tag;
        if (!ReadText(bytes,pos,tag,32) || !ValidPageName(tag) || tag.find(L',')!=std::wstring::npos ||
            std::any_of(p.tags.begin(),p.tags.end(),[&](const auto &prior){return SamePageName(prior,tag);})) return false;
        p.tags.push_back(std::move(tag));
      }
    }
    if (!names.insert(p.name).second || !identities.insert(p.id).second) return false;
    parsed.push_back(std::move(p));
  }
  if (pos != bytes.size()) return false;
  pages = std::move(parsed); return true;
}

// Atomic auxiliary snapshots are used for crash drafts. Never follow a link or
// replace a file whose identity/content changed while staging the write.
inline bool RenameStagedFile(HANDLE file,const std::filesystem::path &path,bool replace) {
  std::error_code error;const auto destination=std::filesystem::absolute(path,error).make_preferred().wstring();
  if(error) return false;
  const size_t size=offsetof(FILE_RENAME_INFO,FileName)+(destination.size()+1)*sizeof(wchar_t);
  std::vector<unsigned char> buffer(size);auto *rename=reinterpret_cast<FILE_RENAME_INFO *>(buffer.data());
  rename->ReplaceIfExists=replace;rename->RootDirectory=nullptr;
  rename->FileNameLength=static_cast<DWORD>(destination.size()*sizeof(wchar_t));
  memcpy(rename->FileName,destination.data(),rename->FileNameLength);
  return SetFileInformationByHandle(file,FileRenameInfo,rename,static_cast<DWORD>(size))!=FALSE;
}
inline void DiscardStagedFile(HANDLE file) {
  if(file!=INVALID_HANDLE_VALUE) {FILE_DISPOSITION_INFO discard{TRUE};SetFileInformationByHandle(file,FileDispositionInfo,&discard,sizeof(discard));CloseHandle(file);}
}
struct AuxiliaryFileLease {
  HANDLE handle=INVALID_HANDLE_VALUE;
  DirectoryIdentity identity;
  AuxiliaryFileLease()=default;AuxiliaryFileLease(const AuxiliaryFileLease &)=delete;AuxiliaryFileLease &operator=(const AuxiliaryFileLease &)=delete;
  ~AuxiliaryFileLease(){if(handle!=INVALID_HANDLE_VALUE) CloseHandle(handle);}
  bool Acquire(const std::filesystem::path &path) {
    handle=CreateFileW(path.c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    DWORD size=0;BY_HANDLE_FILE_INFORMATION info{};
    if(handle==INVALID_HANDLE_VALUE || !SafeFileHandle(handle,size,kMaxNotebookBytes) || !GetFileInformationByHandle(handle,&info)) return false;
    identity={info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow,true};return true;
  }
  bool Remove(){FILE_DISPOSITION_INFO discard{TRUE};return handle!=INVALID_HANDLE_VALUE && SetFileInformationByHandle(handle,FileDispositionInfo,&discard,sizeof(discard))!=FALSE;}
};
inline bool PublishStagedFile(HANDLE &stage,const std::filesystem::path &staged,const std::filesystem::path &path,const FileSnapshot &expected,const std::string &bytes,DirectoryIdentity *committedFile=nullptr) {
  if(expected.status==ReadStatus::Missing) return RenameStagedFile(stage,path,false);
  if(expected.status!=ReadStatus::Ok) return false;
  struct ReadGuard {HANDLE handle=INVALID_HANDLE_VALUE;~ReadGuard(){if(handle!=INVALID_HANDLE_VALUE) CloseHandle(handle);}} original;
  original.handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  DWORD originalSize=0;if(original.handle==INVALID_HANDLE_VALUE || !SafeFileHandle(original.handle,originalSize,kMaxNotebookBytes)) return false;
  BY_HANDLE_FILE_INFORMATION sourceInfo{};if(!GetFileInformationByHandle(stage,&sourceInfo)) return false;
  const DirectoryIdentity sourceIdentity{sourceInfo.dwVolumeSerialNumber,sourceInfo.nFileIndexHigh,sourceInfo.nFileIndexLow,true};
  std::filesystem::path recovery;
  for(unsigned attempt=0;attempt<16;++attempt) {
    recovery=path.parent_path()/(L".ctSpaces-save-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(attempt));
    if(CreateDirectoryW(recovery.c_str(),nullptr)) break;
    if(GetLastError()!=ERROR_ALREADY_EXISTS) return false;
    recovery.clear();
  }
  if(recovery.empty()) return false;
  NotebookWriteRoot protectedRecovery;if(!protectedRecovery.Acquire(recovery)) {RetainedRecovery(recovery);return false;}
  const auto previous=recovery/L"previous",rollback=recovery/L"rollback";
  struct OwnerRecord {HANDLE handle=INVALID_HANDLE_VALUE;~OwnerRecord(){if(handle!=INVALID_HANDLE_VALUE) CloseHandle(handle);}} owner;
  const auto cleanupDirectory=[&] {
    if(owner.handle!=INVALID_HANDLE_VALUE){FILE_DISPOSITION_INFO remove{TRUE};SetFileInformationByHandle(owner.handle,FileDispositionInfo,&remove,sizeof(remove));CloseHandle(owner.handle);owner.handle=INVALID_HANDLE_VALUE;}
    if(protectedRecovery.sentinel!=INVALID_HANDLE_VALUE){CloseHandle(protectedRecovery.sentinel);protectedRecovery.sentinel=INVALID_HANDLE_VALUE;}
    protectedRecovery.lease.Release();
    HANDLE directory=CreateFileW(recovery.c_str(),DELETE|FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    if(directory!=INVALID_HANDLE_VALUE){if(GetFileInformationByHandle(directory,&info) && DirectoryIdentity{info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow,true}==protectedRecovery.lease.identity && !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){FILE_DISPOSITION_INFO remove{TRUE};SetFileInformationByHandle(directory,FileDispositionInfo,&remove,sizeof(remove));}CloseHandle(directory);}
  };
  std::string ownerBytes;
  if(path.filename().wstring().size()>255 || !EncodeUtf8(path.filename().wstring(),ownerBytes)){cleanupDirectory();return false;}
  ownerBytes="CTNSAVE1"+ownerBytes;
  owner.handle=CreateFileW((recovery/L"target").c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
  DWORD recorded=0;
  if(owner.handle==INVALID_HANDLE_VALUE || !WriteFile(owner.handle,ownerBytes.data(),static_cast<DWORD>(ownerBytes.size()),&recorded,nullptr) || recorded!=ownerBytes.size() || !FlushFileBuffers(owner.handle)){cleanupDirectory();return false;}
  if(GetFileAttributesW(previous.c_str())!=INVALID_FILE_ATTRIBUTES || GetLastError()!=ERROR_FILE_NOT_FOUND || !SameSnapshot(expected,ReadFileSnapshot(path.parent_path(),path.filename().c_str(),kMaxNotebookBytes))) {cleanupDirectory();return false;}
  CloseHandle(stage);stage=INVALID_HANDLE_VALUE;
  const auto native=[](const std::filesystem::path &value){return std::filesystem::absolute(value).make_preferred().wstring();};
  const bool published=ReplaceFileW(native(path).c_str(),native(staged).c_str(),native(previous).c_str(),REPLACEFILE_IGNORE_MERGE_ERRORS,nullptr,nullptr)!=FALSE;
  const auto displaced=ReadFileSnapshot(recovery,L"previous",kMaxNotebookBytes);
  AuxiliaryFileLease installed;const bool installedOk=installed.Acquire(path);
  const auto current=ReadFileSnapshot(path.parent_path(),path.filename().c_str(),kMaxNotebookBytes);
  const bool ours=installedOk && installed.identity==sourceIdentity && current.status==ReadStatus::Ok && current.bytes==bytes;
  if(published && ours && SameSnapshot(displaced,expected)) {
    if(committedFile) *committedFile=sourceIdentity;
    AuxiliaryFileLease old;if(old.Acquire(previous) && SameSnapshot(displaced,ReadFileSnapshot(recovery,L"previous",kMaxNotebookBytes))) old.Remove();
    if(old.handle!=INVALID_HANDLE_VALUE){CloseHandle(old.handle);old.handle=INVALID_HANDLE_VALUE;}
    if(original.handle!=INVALID_HANDLE_VALUE){CloseHandle(original.handle);original.handle=INVALID_HANDLE_VALUE;}
    if(installed.handle!=INVALID_HANDLE_VALUE){CloseHandle(installed.handle);installed.handle=INVALID_HANDLE_VALUE;}
    cleanupDirectory();return true;
  }
  if(!published && displaced.status==ReadStatus::Missing && SameSnapshot(current,expected)) {
    if(installed.handle!=INVALID_HANDLE_VALUE){CloseHandle(installed.handle);installed.handle=INVALID_HANDLE_VALUE;}
    AuxiliaryFileLease pending;if(pending.Acquire(staged) && pending.identity==sourceIdentity) pending.Remove();
    if(pending.handle!=INVALID_HANDLE_VALUE){CloseHandle(pending.handle);pending.handle=INVALID_HANDLE_VALUE;}
    cleanupDirectory();return false;
  }
  // Atomic native replacement retains the file actually displaced. A late
  // writer is restored only while our own exact stage is still installed;
  // every displaced version remains in the owned recovery directory on error.
  if(installed.handle!=INVALID_HANDLE_VALUE){CloseHandle(installed.handle);installed.handle=INVALID_HANDLE_VALUE;}
  if(displaced.status==ReadStatus::Ok) {
    if(original.handle!=INVALID_HANDLE_VALUE){CloseHandle(original.handle);original.handle=INVALID_HANDLE_VALUE;}
    if(current.status==ReadStatus::Missing){AuxiliaryFileLease old;if(old.Acquire(previous)) RenameStagedFile(old.handle,path,false);}
    else if(ours && GetFileAttributesW(rollback.c_str())==INVALID_FILE_ATTRIBUTES && GetLastError()==ERROR_FILE_NOT_FOUND)
      ReplaceFileW(native(path).c_str(),native(previous).c_str(),native(rollback).c_str(),REPLACEFILE_IGNORE_MERGE_ERRORS,nullptr,nullptr);
  }
  RetainedRecovery(recovery);
  return false;
}
inline bool DeleteAuxiliary(const std::filesystem::path &root,const std::wstring &name,const DirectoryIdentity &directory,const DirectoryIdentity &fileIdentity) {
  if(!directory.valid || !fileIdentity.valid || name.find_first_of(L"/\\:")!=std::wstring::npos) return false;
  NotebookDirectoryLease lease;AuxiliaryFileLease file;
  return lease.Acquire(root) && lease.identity==directory && file.Acquire(root/name) && file.identity==fileIdentity && file.Remove();
}
inline bool WriteAuxiliary(const std::filesystem::path &root, const std::wstring &name,
                           const std::string &bytes,DirectoryIdentity *committedFile=nullptr) {
  persistenceError.clear();
  NotebookWriteRoot guard;
  NotebookTransaction transaction;
  if (!guard.Acquire(root) || !transaction.Acquire(guard.lease.identity) || !SafeDirectory(root) || bytes.size() > kMaxNotebookBytes || name.find_first_of(L"/\\:") != std::wstring::npos) return false;
  const auto expected = ReadFileSnapshot(root, name.c_str(), kMaxNotebookBytes);
  if (expected.status == ReadStatus::Error) return false;
  const auto stage = root / (name + L".stage-" + std::to_wstring(GetCurrentProcessId()));
  HANDLE h = CreateFileW(stage.c_str(),GENERIC_WRITE|DELETE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  bool ok = WriteFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) && written == bytes.size() && FlushFileBuffers(h);
  ok = ok && SafeDirectory(root) && SameSnapshot(expected,ReadFileSnapshot(root,name.c_str(),kMaxNotebookBytes));
  if (ok) ok = PublishStagedFile(h,stage,root/name,expected,bytes,committedFile);
  if(!ok) {DiscardStagedFile(h);return false;}
  if(h!=INVALID_HANDLE_VALUE) {if(committedFile) {BY_HANDLE_FILE_INFORMATION info{};*committedFile={};if(GetFileInformationByHandle(h,&info)) *committedFile={info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow,true};}return CloseHandle(h)!=FALSE;}
  return true;
}
inline NotebookResult ReadNotebook(const std::filesystem::path &root) {
  persistenceError.clear();
  NotebookResult result;
  NotebookDirectoryLease lease;
  NotebookTransaction transaction;
  if(!lease.Acquire(root) || !transaction.Acquire(lease.identity)) return result;
  result.directory_identity=lease.identity;
  result.notebook_snapshot =
      ReadFileSnapshot(root, kNotebookFileName, kMaxNotebookBytes);
  if (result.notebook_snapshot.status == ReadStatus::Error)
    return result;
  if (result.notebook_snapshot.status == ReadStatus::Ok) {
    if (ParseNotebook(result.notebook_snapshot.bytes, result.pages))
      result.status = ReadStatus::Ok;
    return result;
  }
  // Once the notebook exists it is authoritative. Obsolete fallback files
  // participate only in the first migration, never in later opens or saves.
  result.plain_snapshot = ReadFileSnapshot(root, kFileName, kMaxBytes,false);
  result.rich_snapshot = ReadFileSnapshot(root, kRichFileName, kMaxRichBytes,false);
  if (result.plain_snapshot.status == ReadStatus::Error ||
      result.rich_snapshot.status == ReadStatus::Error)
    return result;
  NotePage initial;
  initial.name = L"Notes";
  initial.id = 1;
  if (result.rich_snapshot.status == ReadStatus::Ok) {
    if (!ValidRichText(result.rich_snapshot.bytes))
      return result;
    initial.rich = true;
    initial.rtf = result.rich_snapshot.bytes;
    result.status = ReadStatus::Ok;
  } else if (result.plain_snapshot.status == ReadStatus::Ok) {
    if (!DecodeUtf8(result.plain_snapshot.bytes, initial.text))
      return result;
    result.status = ReadStatus::Ok;
  } else {
    result.status = ReadStatus::Missing;
  }
  result.pages.push_back(std::move(initial));
  return result;
}

inline bool WriteNotebook(const std::filesystem::path &root,
                          const std::vector<NotePage> &pages,
                          const NotebookResult &expected,
                          const std::function<bool()> &revalidate = {}) {
  persistenceError.clear();
  std::lock_guard<std::mutex> lock(mutex);
  NotebookWriteRoot guard;
  NotebookTransaction transaction;
  std::string bytes;
  if (!guard.Acquire(root) || !expected.directory_identity.valid || guard.lease.identity!=expected.directory_identity || !transaction.Acquire(guard.lease.identity) ||
      !SafeDirectory(root) || !SerializeNotebook(pages, bytes) ||
      expected.status == ReadStatus::Error ||
      (revalidate && !revalidate()))
    return false;
  const bool migration=expected.notebook_snapshot.status==ReadStatus::Missing;
  struct LegacyGuards {std::vector<HANDLE> handles;~LegacyGuards(){for(HANDLE handle:handles) CloseHandle(handle);}} legacy;
  if(migration) for(const auto &[name,snapshot,limit]:{
      std::tuple<const wchar_t *,const FileSnapshot *,DWORD>{kFileName,&expected.plain_snapshot,kMaxBytes},
      {kRichFileName,&expected.rich_snapshot,kMaxRichBytes}}) {
    if(snapshot->status==ReadStatus::Missing) continue;
    if(snapshot->status!=ReadStatus::Ok) return false;
    HANDLE file=CreateFileW((root/name).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    DWORD length=0;if(file==INVALID_HANDLE_VALUE) return false;legacy.handles.push_back(file);
    if(!SafeFileHandle(file,length,limit)) return false;
  }
  const auto legacyMatches=[&] {
    return SameSnapshot(expected.plain_snapshot,ReadFileSnapshot(root,kFileName,kMaxBytes,false)) &&
           SameSnapshot(expected.rich_snapshot,ReadFileSnapshot(root,kRichFileName,kMaxRichBytes,false));
  };
  const auto matches = [&]() {
    const NotebookResult current = ReadNotebook(root);
    return current.status != ReadStatus::Error &&
           (!migration || (SameSnapshot(current.plain_snapshot, expected.plain_snapshot) &&
                           SameSnapshot(current.rich_snapshot, expected.rich_snapshot))) &&
           SameSnapshot(current.notebook_snapshot, expected.notebook_snapshot);
  };
  if (!matches())
    return false;
  const auto path = root / kNotebookFileName;
  for (unsigned attempt = 0; attempt < 16; ++attempt) {
    const auto stage = root /
        (L".ctSpaces-notebook-" + std::to_wstring(GetCurrentProcessId()) +
         L"-" + std::to_wstring(GetTickCount64()) + L"-" +
         std::to_wstring(attempt));
    HANDLE file = CreateFileW(stage.c_str(), GENERIC_WRITE|DELETE, FILE_SHARE_READ, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL |
                                              FILE_FLAG_WRITE_THROUGH,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      if (GetLastError() == ERROR_FILE_EXISTS ||
          GetLastError() == ERROR_ALREADY_EXISTS)
        continue;
      return false;
    }
    DWORD written = 0;
    const bool saved = WriteFile(file, bytes.data(),
                                 static_cast<DWORD>(bytes.size()), &written,
                                 nullptr) && written == bytes.size() &&
                       FlushFileBuffers(file) != FALSE;
    const bool ready = saved && SafeDirectory(root) &&
                       (!revalidate || revalidate()) && matches();
    const bool moved=ready && PublishStagedFile(file,stage,path,expected.notebook_snapshot,bytes);
    if (!moved) {
      DiscardStagedFile(file);
      return false;
    }
    // A source that was absent cannot be pinned. While the first CTN is still
    // our held original stage, reject late source creation and remove only our
    // exact new file so the newer legacy source remains authoritative.
    if(migration && !legacyMatches()) {DiscardStagedFile(file);file=INVALID_HANDLE_VALUE;return false;}
    return file==INVALID_HANDLE_VALUE || CloseHandle(file)!=FALSE;
  }
  return false;
}
} // namespace client_notes
