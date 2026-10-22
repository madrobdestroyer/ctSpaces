#include "ConfigPersistence.h"
#include "SiblingStageName.h"

#include <windows.h>

#include <format>
#include <memory>
#include <mutex>
#include <new>
#include <span>
#include <system_error>
#include <vector>

#pragma comment(lib, "ole32.lib")

namespace config_persistence {
namespace {

std::mutex g_iniMutationMutex;
#ifdef CTSPACES_CONFIG_TEST_HOOKS
thread_local IniPublicationHook publicationHook=nullptr;
thread_local void* publicationHookUser=nullptr;
thread_local IniPublicationFailure publicationFailure=IniPublicationFailure::None;
#endif

constexpr LONGLONG kMaximumIniMigrationBytes = 16LL * 1024 * 1024;

bool IsMissingError(DWORD error) {
  return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

void StoreWindowsError(unsigned long *destination, DWORD error) {
  if (destination)
    *destination = error;
}

void StoreFailure(std::wstring *details, const wchar_t *operation,
                  DWORD error) {
  if (!details)
    return;
  *details = std::format(L"{} (Windows error {}).", operation,
                         error ? error : ERROR_WRITE_FAULT);
}

bool ReadAll(HANDLE file, std::vector<BYTE> &contents, DWORD &error) {
  DWORD total = 0;
  while (total < contents.size()) {
    DWORD read = 0;
    const DWORD remaining =
        static_cast<DWORD>(contents.size() - static_cast<size_t>(total));
    if (!ReadFile(file, contents.data() + total, remaining, &read, nullptr)) {
      error = GetLastError();
      return false;
    }
    if (read == 0) {
      error = ERROR_HANDLE_EOF;
      return false;
    }
    total += read;
  }
  return true;
}

struct ConfigSnapshot {
  HANDLE file = INVALID_HANDLE_VALUE;
  bool missing = false;
  BY_HANDLE_FILE_INFORMATION identity{};
  std::vector<BYTE> bytes;
  ~ConfigSnapshot() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
  void Close() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); file = INVALID_HANDLE_VALUE; }
};

struct ConfigDirectoryLease {
  HANDLE file = INVALID_HANDLE_VALUE;
  ~ConfigDirectoryLease(){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
};

bool PinConfigAncestors(const std::filesystem::path &path,
                        std::vector<std::unique_ptr<ConfigDirectoryLease>> &leases,
                        std::wstring *details) {
  const auto absolute=std::filesystem::absolute(path).lexically_normal();
  std::filesystem::path part=absolute.root_path();
  auto pin=[&]() {
    auto lease=std::make_unique<ConfigDirectoryLease>();
    lease->file=CreateFileW(part.c_str(),GENERIC_READ,
        FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    if(lease->file==INVALID_HANDLE_VALUE ||
       !GetFileInformationByHandle(lease->file,&info) ||
       !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) ||
       (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) {
      StoreFailure(details,L"The configuration ancestor could not be protected",GetLastError());
      return false;
    }
    leases.push_back(std::move(lease));return true;
  };
  if(!pin())return false;
  for(const auto& component:absolute.relative_path()){part/=component;if(!pin())return false;}
  return true;
}

std::filesystem::path NativeConfigPath(const std::filesystem::path &path) {
  const auto absolute=std::filesystem::absolute(path).lexically_normal().wstring();
  if(absolute.starts_with(L"\\\\?\\"))return absolute;
  if(absolute.starts_with(L"\\\\"))return L"\\\\?\\UNC\\"+absolute.substr(2);
  return L"\\\\?\\"+absolute;
}

bool AllowConfigDirectoryWrites(const std::filesystem::path& path,
                                ConfigDirectoryLease& original,
                                std::wstring* details) {
  BY_HANDLE_FILE_INFORMATION before{},after{};
  ConfigDirectoryLease compatible;
  compatible.file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
      nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
  if(compatible.file==INVALID_HANDLE_VALUE || !GetFileInformationByHandle(original.file,&before) ||
     !GetFileInformationByHandle(compatible.file,&after) ||
     before.dwVolumeSerialNumber!=after.dwVolumeSerialNumber || before.nFileIndexHigh!=after.nFileIndexHigh ||
     before.nFileIndexLow!=after.nFileIndexLow || !(after.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) ||
     (after.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) {
    StoreFailure(details,L"The protected configuration directory changed",ERROR_FILE_INVALID);return false;
  }
  CloseHandle(original.file);original.file=compatible.file;compatible.file=INVALID_HANDLE_VALUE;return true;
}

bool CaptureConfigSnapshot(const std::filesystem::path &path,
                          ConfigSnapshot &snapshot,
                          std::wstring *errorDetails,
                          size_t maximumBytes) {
  // Permit replacement detection, but prevent in-place writes while this
  // particular file object is read and compared.
  snapshot.file = CreateFileW(path.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (snapshot.file == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    if (IsMissingError(error)) { snapshot.missing = true; return true; }
    StoreFailure(errorDetails, L"The configuration snapshot could not be read", error);
    return false;
  }
  LARGE_INTEGER size{};
  if (!GetFileInformationByHandle(snapshot.file, &snapshot.identity) ||
      (snapshot.identity.dwFileAttributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
      !GetFileSizeEx(snapshot.file, &size) || size.QuadPart < 0 ||
      size.QuadPart > static_cast<LONGLONG>(maximumBytes)) {
    StoreFailure(errorDetails, L"The configuration snapshot is unavailable or unsafe", ERROR_INVALID_DATA);
    return false;
  }
  try {
    snapshot.bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD error = ERROR_SUCCESS;
    if (!ReadAll(snapshot.file, snapshot.bytes, error)) {
      StoreFailure(errorDetails, L"The configuration snapshot could not be read", error);
      return false;
    }
  } catch (...) {
    StoreFailure(errorDetails, L"The configuration snapshot could not be retained", ERROR_NOT_ENOUGH_MEMORY);
    return false;
  }
  return true;
}

bool SameConfigSnapshot(const ConfigSnapshot &left, const ConfigSnapshot &right) {
  if (left.missing || right.missing) return left.missing == right.missing;
  return left.identity.dwVolumeSerialNumber == right.identity.dwVolumeSerialNumber &&
      left.identity.nFileIndexHigh == right.identity.nFileIndexHigh &&
      left.identity.nFileIndexLow == right.identity.nFileIndexLow &&
      left.bytes == right.bytes;
}

bool DeleteMatchingConfigSnapshot(const std::filesystem::path& path,
                                  const ConfigSnapshot& expected) {
  ConfigSnapshot bound;
  bound.file=CreateFileW(path.c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,
      nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(bound.file==INVALID_HANDLE_VALUE)return IsMissingError(GetLastError());
  if(!GetFileInformationByHandle(bound.file,&bound.identity) ||
     (bound.identity.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) ||
     bound.identity.dwVolumeSerialNumber!=expected.identity.dwVolumeSerialNumber ||
     bound.identity.nFileIndexHigh!=expected.identity.nFileIndexHigh ||
     bound.identity.nFileIndexLow!=expected.identity.nFileIndexLow)return false;
  LARGE_INTEGER size{};
  if(!GetFileSizeEx(bound.file,&size) || size.QuadPart!=static_cast<LONGLONG>(expected.bytes.size()))return false;
  bound.bytes.resize(expected.bytes.size());DWORD error=ERROR_SUCCESS;
  if(!ReadAll(bound.file,bound.bytes,error) || bound.bytes!=expected.bytes)return false;
  FILE_DISPOSITION_INFO disposition{TRUE};
  return SetFileInformationByHandle(bound.file,FileDispositionInfo,&disposition,sizeof(disposition))!=FALSE;
}

bool RenameMatchingConfigSnapshot(const std::filesystem::path& path,
                                  const ConfigSnapshot& expected,
                                  const std::filesystem::path& destination) {
  ConfigSnapshot bound;
  bound.file=CreateFileW(path.c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,
      nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(bound.file==INVALID_HANDLE_VALUE || !GetFileInformationByHandle(bound.file,&bound.identity) ||
     (bound.identity.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) ||
     bound.identity.dwVolumeSerialNumber!=expected.identity.dwVolumeSerialNumber ||
     bound.identity.nFileIndexHigh!=expected.identity.nFileIndexHigh || bound.identity.nFileIndexLow!=expected.identity.nFileIndexLow)return false;
  LARGE_INTEGER size{};
  if(!GetFileSizeEx(bound.file,&size) || size.QuadPart!=static_cast<LONGLONG>(expected.bytes.size()))return false;
  bound.bytes.resize(expected.bytes.size());DWORD error=ERROR_SUCCESS;
  if(!ReadAll(bound.file,bound.bytes,error) || bound.bytes!=expected.bytes)return false;
  const auto name=NativeConfigPath(destination).wstring();
  std::vector<BYTE> data(offsetof(FILE_RENAME_INFO,FileName)+(name.size()+1)*sizeof(wchar_t),0);
  auto* rename=reinterpret_cast<FILE_RENAME_INFO*>(data.data());
  rename->ReplaceIfExists=FALSE;rename->FileNameLength=static_cast<DWORD>(name.size()*sizeof(wchar_t));
  memcpy(rename->FileName,name.data(),rename->FileNameLength);
  return SetFileInformationByHandle(bound.file,FileRenameInfo,rename,static_cast<DWORD>(data.size()))!=FALSE;
}

BOOL PublishConfigReplacement(const std::filesystem::path& config,
                              const std::filesystem::path& staged,
                              const std::filesystem::path& backup,
                              const ConfigSnapshot& current) {
#ifdef CTSPACES_CONFIG_TEST_HOOKS
  const auto failure=publicationFailure;publicationFailure=IniPublicationFailure::None;
  if(failure==IniPublicationFailure::OldDisplaced) {
    if(!RenameMatchingConfigSnapshot(config,current,backup))return FALSE;
    SetLastError(ERROR_UNABLE_TO_MOVE_REPLACEMENT_2);return FALSE;
  }
#else
  (void)current;
#endif
  const BOOL published=ReplaceFileW(config.c_str(),staged.c_str(),backup.c_str(),
                    REPLACEFILE_IGNORE_MERGE_ERRORS,nullptr,nullptr);
#ifdef CTSPACES_CONFIG_TEST_HOOKS
  if(published && failure==IniPublicationFailure::Installed) {
    SetLastError(ERROR_WRITE_FAULT);return FALSE;
  }
#endif
  return published;
}

bool NewConfigStageName(const std::filesystem::path &configPath,
                       unsigned attempt, std::filesystem::path &stage,
                       std::wstring *errorDetails) {
  GUID identity{};
  const HRESULT hr = CoCreateGuid(&identity);
  if (FAILED(hr)) {
    StoreFailure(errorDetails, L"A unique configuration stage could not be named", ERROR_NOT_ENOUGH_MEMORY);
    return false;
  }
  constexpr wchar_t digits[] = L"0123456789abcdef";
  std::wstring token;
  for (const BYTE byte : std::span<const BYTE>(reinterpret_cast<const BYTE *>(&identity), sizeof(identity))) {
    token.push_back(digits[byte >> 4]); token.push_back(digits[byte & 15]);
  }
  const size_t budget = configPath.filename().native().size();
  const auto component = budget == 1 ?
      sibling_stage_name::BuildSingleCharacter(identity.Data1 + attempt, false, budget) :
      sibling_stage_name::Build(token, false, budget);
  if (component.empty()) {
    StoreFailure(errorDetails, L"The configuration filename leaves no staging budget", ERROR_INVALID_NAME);
    return false;
  }
  stage = configPath.parent_path() / component;
  return true;
}

bool ProtectConfigDirectoryContents(const std::filesystem::path& representative,
                                    ConfigDirectoryLease& sentinel,
                                    std::wstring* details) {
  for(unsigned attempt=0;attempt<32;++attempt) {
    std::filesystem::path candidate;
    if(!NewConfigStageName(representative,attempt,candidate,details))return false;
    if(_wcsicmp(candidate.filename().c_str(),representative.filename().c_str())==0)continue;
    const auto native=NativeConfigPath(candidate);
    sentinel.file=CreateFileW(native.c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,
        nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
    if(sentinel.file!=INVALID_HANDLE_VALUE)return true;
    const DWORD error=GetLastError();
    if(error!=ERROR_ALREADY_EXISTS && error!=ERROR_FILE_EXISTS) {
      StoreFailure(details,L"The configuration directory guard could not be reserved",error);return false;
    }
  }
  StoreFailure(details,L"The configuration directory guard could not be reserved",ERROR_ALREADY_EXISTS);return false;
}

bool WriteAll(HANDLE file, const void *contents, DWORD byteCount,
              DWORD &error) {
  const auto *bytes = static_cast<const BYTE *>(contents);
  DWORD total = 0;
  while (total < byteCount) {
    DWORD written = 0;
    if (!WriteFile(file, bytes + total, byteCount - total, &written, nullptr)) {
      error = GetLastError();
      return false;
    }
    if (written == 0) {
      error = ERROR_WRITE_FAULT;
      return false;
    }
    total += written;
  }
  return true;
}

bool PrepareUnicodeIniStage(const std::filesystem::path &path,
                            std::wstring *errorDetails) {
  HANDLE file = CreateFileW(
      path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
          FILE_FLAG_WRITE_THROUGH,
      nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    StoreFailure(errorDetails,
                 L"The staged configuration could not be opened for Unicode "
                 L"preparation",
                 GetLastError());
    return false;
  }

  const auto fail = [&](const wchar_t *operation, DWORD error) {
    CloseHandle(file);
    StoreFailure(errorDetails, operation, error);
    return false;
  };

  BY_HANDLE_FILE_INFORMATION information{};
  if (!GetFileInformationByHandle(file, &information)) {
    return fail(L"The staged configuration is not a safe regular file",
                GetLastError());
  }
  if ((information.dwFileAttributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
    return fail(L"The staged configuration is not a safe regular file",
                ERROR_ACCESS_DENIED);
  }

  LARGE_INTEGER fileSize{};
  if (!GetFileSizeEx(file, &fileSize)) {
    return fail(L"The staged configuration size could not be read",
                GetLastError());
  }
  if (fileSize.QuadPart < 0 ||
      fileSize.QuadPart > kMaximumIniMigrationBytes ||
      fileSize.QuadPart > static_cast<LONGLONG>(MAXDWORD)) {
    return fail(L"The staged configuration is too large to migrate safely",
                ERROR_FILE_TOO_LARGE);
  }

  try {
    std::vector<BYTE> original(static_cast<size_t>(fileSize.QuadPart));
    DWORD ioError = ERROR_SUCCESS;
    if (!ReadAll(file, original, ioError)) {
      return fail(L"The staged configuration could not be read", ioError);
    }

    if (original.size() >= 4 &&
        ((original[0] == 0xFF && original[1] == 0xFE &&
          original[2] == 0x00 && original[3] == 0x00) ||
         (original[0] == 0x00 && original[1] == 0x00 &&
          original[2] == 0xFE && original[3] == 0xFF))) {
      return fail(L"The staged configuration uses unsupported UTF-32 text",
                  ERROR_NO_UNICODE_TRANSLATION);
    }
    if (original.size() >= 2 && original[0] == 0xFF &&
        original[1] == 0xFE) {
      if ((original.size() - 2) % sizeof(wchar_t) != 0) {
        return fail(L"The staged Unicode configuration has a partial code unit",
                    ERROR_INVALID_DATA);
      }
      for (size_t index = 2; index < original.size(); index += 2) {
        if (original[index] == 0x00 && original[index + 1] == 0x00) {
          return fail(L"The staged Unicode configuration contains an embedded "
                      L"null character",
                      ERROR_INVALID_DATA);
        }
      }
      if (!CloseHandle(file)) {
        StoreFailure(errorDetails,
                     L"The staged Unicode configuration could not be closed",
                     GetLastError());
        return false;
      }
      return true;
    }
    if (original.size() >= 2 && original[0] == 0xFE &&
        original[1] == 0xFF) {
      return fail(L"The staged configuration uses unsupported big-endian "
                  L"Unicode text",
                  ERROR_NO_UNICODE_TRANSLATION);
    }

    UINT sourceCodePage = CP_ACP;
    size_t sourceOffset = 0;
    const wchar_t *sourceEncodingName = L"ANSI";
    if (original.size() >= 3 && original[0] == 0xEF &&
        original[1] == 0xBB && original[2] == 0xBF) {
      sourceCodePage = CP_UTF8;
      sourceOffset = 3;
      sourceEncodingName = L"UTF-8";
    }

    std::wstring converted;
    const size_t sourceByteCount = original.size() - sourceOffset;
    if (sourceByteCount > 0) {
      SetLastError(ERROR_SUCCESS);
      const int characterCount = MultiByteToWideChar(
          sourceCodePage, MB_ERR_INVALID_CHARS,
          reinterpret_cast<const char *>(original.data() + sourceOffset),
          static_cast<int>(sourceByteCount), nullptr, 0);
      if (characterCount <= 0) {
        const DWORD decodeError = GetLastError();
        return fail((std::wstring(L"The staged ") + sourceEncodingName +
                     L" configuration could not be decoded")
                        .c_str(),
                    decodeError ? decodeError
                                : ERROR_NO_UNICODE_TRANSLATION);
      }
      converted.resize(static_cast<size_t>(characterCount));
      SetLastError(ERROR_SUCCESS);
      if (MultiByteToWideChar(
              sourceCodePage, MB_ERR_INVALID_CHARS,
              reinterpret_cast<const char *>(original.data() + sourceOffset),
              static_cast<int>(sourceByteCount), converted.data(),
              characterCount) != characterCount) {
        const DWORD decodeError = GetLastError();
        return fail((std::wstring(L"The staged ") + sourceEncodingName +
                     L" configuration could not be decoded")
                        .c_str(),
                    decodeError ? decodeError
                                : ERROR_NO_UNICODE_TRANSLATION);
      }
      if (converted.find(L'\0') != std::wstring::npos) {
        return fail(L"The staged text configuration contains an embedded null "
                    L"character",
                    ERROR_INVALID_DATA);
      }
    }

    LARGE_INTEGER beginning{};
    if (!SetFilePointerEx(file, beginning, nullptr, FILE_BEGIN)) {
      return fail(L"The staged configuration could not be rewound",
                  GetLastError());
    }
    if (!SetEndOfFile(file)) {
      return fail(L"The staged configuration could not be prepared for "
                  L"Unicode text",
                  GetLastError());
    }

    constexpr BYTE unicodeBom[] = {0xFF, 0xFE};
    if (!WriteAll(file, unicodeBom, static_cast<DWORD>(sizeof(unicodeBom)),
                  ioError)) {
      return fail(L"The staged Unicode signature could not be written",
                  ioError);
    }
    const size_t convertedBytes = converted.size() * sizeof(wchar_t);
    if (convertedBytes > MAXDWORD) {
      return fail(L"The staged Unicode configuration is too large",
                  ERROR_FILE_TOO_LARGE);
    }
    if (convertedBytes > 0 &&
        !WriteAll(file, converted.data(), static_cast<DWORD>(convertedBytes),
                  ioError)) {
      return fail(L"The staged Unicode configuration could not be written",
                  ioError);
    }
    if (!FlushFileBuffers(file)) {
      return fail(L"The staged Unicode configuration could not be flushed",
                  GetLastError());
    }
    if (!CloseHandle(file)) {
      StoreFailure(errorDetails,
                   L"The staged Unicode configuration could not be closed",
                   GetLastError());
      return false;
    }
    return true;
  } catch (const std::bad_alloc &) {
    return fail(L"The staged configuration could not be migrated to Unicode",
                ERROR_NOT_ENOUGH_MEMORY);
  } catch (...) {
    return fail(L"The staged configuration could not be migrated to Unicode",
                ERROR_INVALID_DATA);
  }
}

bool IsSafeExistingDirectory(const std::filesystem::path &path,
                             DWORD &error) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    error = GetLastError();
    return false;
  }
  if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
      (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    error = ERROR_ACCESS_DENIED;
    return false;
  }
  error = ERROR_SUCCESS;
  return true;
}

bool RemoveStageFile(const std::filesystem::path &path,
                      const BY_HANDLE_FILE_INFORMATION *expected) {
  if (path.empty())
    return true;
  ConfigSnapshot bound;
  bound.file=CreateFileW(path.c_str(),GENERIC_READ|DELETE|FILE_WRITE_ATTRIBUTES,
      FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(bound.file==INVALID_HANDLE_VALUE)return IsMissingError(GetLastError());
  if(!expected || !GetFileInformationByHandle(bound.file,&bound.identity) ||
     (bound.identity.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) ||
     bound.identity.nNumberOfLinks!=1 || bound.identity.dwVolumeSerialNumber!=expected->dwVolumeSerialNumber ||
     bound.identity.nFileIndexHigh!=expected->nFileIndexHigh || bound.identity.nFileIndexLow!=expected->nFileIndexLow)return false;
  FILE_BASIC_INFO basic{};basic.FileAttributes=FILE_ATTRIBUTE_NORMAL;
  if(!SetFileInformationByHandle(bound.file,FileBasicInfo,&basic,sizeof(basic)))return false;
  FILE_DISPOSITION_INFO disposition{TRUE};
  return SetFileInformationByHandle(bound.file,FileDispositionInfo,&disposition,sizeof(disposition))!=FALSE;
}

bool FlushRegularFile(const std::filesystem::path &path,
                      std::wstring *errorDetails) {
  HANDLE file = CreateFileW(
      path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
          FILE_FLAG_WRITE_THROUGH,
      nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    StoreFailure(errorDetails, L"The staged configuration could not be opened",
                 GetLastError());
    return false;
  }

  BY_HANDLE_FILE_INFORMATION information{};
  const bool safeRegularFile =
      GetFileInformationByHandle(file, &information) != FALSE &&
      (information.dwFileAttributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
  if (!safeRegularFile) {
    const DWORD error = GetLastError();
    CloseHandle(file);
    StoreFailure(errorDetails,
                 L"The staged configuration is not a safe regular file",
                 error ? error : ERROR_ACCESS_DENIED);
    return false;
  }

  if (!FlushFileBuffers(file)) {
    const DWORD error = GetLastError();
    CloseHandle(file);
    StoreFailure(errorDetails, L"The staged configuration could not be flushed",
                 error);
    return false;
  }
  if (!CloseHandle(file)) {
    StoreFailure(errorDetails, L"The staged configuration could not be closed",
                 GetLastError());
    return false;
  }
  return true;
}

} // namespace

#ifdef CTSPACES_CONFIG_TEST_HOOKS
void SetIniPublicationHookForTest(IniPublicationHook hook,void* user) {
  publicationHook=hook;publicationHookUser=user;
}
void FailIniPublicationForTest(IniPublicationFailure failure) {publicationFailure=failure;}
#endif

ConfigFileState InspectConfigFile(const std::filesystem::path &configPath,
                                  unsigned long *windowsError) {
  StoreWindowsError(windowsError, ERROR_SUCCESS);
  if (configPath.empty()) {
    StoreWindowsError(windowsError, ERROR_INVALID_NAME);
    return ConfigFileState::Unavailable;
  }

  const DWORD attributes = GetFileAttributesW(configPath.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    const DWORD error = GetLastError();
    StoreWindowsError(windowsError, error);
    return IsMissingError(error) ? ConfigFileState::Missing
                                 : ConfigFileState::Unavailable;
  }
  if ((attributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
    StoreWindowsError(windowsError, ERROR_ACCESS_DENIED);
    return ConfigFileState::Unavailable;
  }

  HANDLE file = CreateFileW(configPath.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE |
                                FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    StoreWindowsError(windowsError, GetLastError());
    return ConfigFileState::Unavailable;
  }

  BY_HANDLE_FILE_INFORMATION information{};
  const bool safeRegularFile =
      GetFileInformationByHandle(file, &information) != FALSE &&
      (information.dwFileAttributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
  DWORD error = safeRegularFile ? ERROR_SUCCESS : GetLastError();
  if (!safeRegularFile && error == ERROR_SUCCESS)
    error = ERROR_ACCESS_DENIED;
  CloseHandle(file);
  StoreWindowsError(windowsError, error);
  return safeRegularFile ? ConfigFileState::Regular
                         : ConfigFileState::Unavailable;
}

static bool MutateFileAtomically(const std::filesystem::path &configPath,
                             const IniStageMutation &mutation,
                             bool normalizeIni, size_t maximumBytes,
                             bool allowCreate,
                             std::wstring *errorDetails) {
  if (errorDetails)
    errorDetails->clear();
  if (!mutation || configPath.empty() || !configPath.has_filename() ||
      maximumBytes == 0 || maximumBytes > MAXDWORD) {
    StoreFailure(errorDetails, L"The configuration update is invalid",
                 ERROR_INVALID_PARAMETER);
    return false;
  }

  std::lock_guard<std::mutex> lock(g_iniMutationMutex);

  std::vector<std::unique_ptr<ConfigDirectoryLease>> ancestry;
  if(!PinConfigAncestors(configPath.parent_path(),ancestry,errorDetails))return false;
  // Native replacement needs write sharing on parent directories. A held,
  // delete-on-close sentinel keeps this directory nonempty throughout the
  // transaction, preventing an in-place junction conversion while retaining
  // compatible directory sharing for normal publication.
  ConfigDirectoryLease directorySentinel;
  if(!ProtectConfigDirectoryContents(configPath,directorySentinel,errorDetails))return false;
  if(!AllowConfigDirectoryWrites(configPath.parent_path(),*ancestry.back(),errorDetails))return false;

  DWORD parentError = ERROR_SUCCESS;
  const std::filesystem::path parentPath = configPath.parent_path();
  if (parentPath.empty() ||
      !IsSafeExistingDirectory(parentPath, parentError)) {
    StoreFailure(errorDetails,
                 L"The configuration folder is unavailable or unsafe",
                 parentError);
    return false;
  }

  unsigned long inspectionError = ERROR_SUCCESS;
  const ConfigFileState originalState =
      InspectConfigFile(configPath, &inspectionError);
  if (originalState == ConfigFileState::Unavailable) {
    StoreFailure(errorDetails,
                 L"The existing configuration is unavailable or unsafe",
                 inspectionError);
    return false;
  }

  ConfigSnapshot expected;
  if (!CaptureConfigSnapshot(configPath, expected, errorDetails, maximumBytes)) return false;
  if (expected.missing && !allowCreate) {
    StoreFailure(errorDetails, L"The file to update is missing", ERROR_FILE_NOT_FOUND);
    return false;
  }
  // The external writer may legitimately edit during staging. Preserve that
  // update by rejecting a stale commit rather than holding this early lock.
  expected.Close();

  std::filesystem::path stagedPath;
  BY_HANDLE_FILE_INFORMATION stageIdentity{};
  struct StageCleanupOwner {
    std::filesystem::path& path;
    BY_HANDLE_FILE_INFORMATION& identity;
    std::wstring* details;
    bool created=false;
    ~StageCleanupOwner(){
      if(!created || path.empty())return;
      if(!RemoveStageFile(path,&identity) && details)
        *details+=L" Changed or unavailable staging data was retained at "+path.wstring()+L".";
    }
  } stageCleanupOwner{stagedPath,stageIdentity,errorDetails};
  for (unsigned attempt = 0; attempt < 32; ++attempt) {
    if (!NewConfigStageName(configPath, attempt, stagedPath, errorDetails)) return false;
    if (_wcsicmp(stagedPath.filename().c_str(), configPath.filename().c_str()) == 0) {
      stagedPath.clear(); continue;
    }
    HANDLE reservation = CreateFileW(
        stagedPath.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH,
        nullptr);
    if (reservation != INVALID_HANDLE_VALUE) {
      if(!GetFileInformationByHandle(reservation,&stageIdentity)) {
        const DWORD error=GetLastError();FILE_DISPOSITION_INFO disposition{TRUE};
        SetFileInformationByHandle(reservation,FileDispositionInfo,&disposition,sizeof(disposition));CloseHandle(reservation);
        StoreFailure(errorDetails,L"The staged configuration identity could not be retained",error);return false;
      }
      stageCleanupOwner.created=true;
      if (!CloseHandle(reservation)) {
        StoreFailure(errorDetails,
                     L"The staged configuration could not be reserved",
                     GetLastError());
        RemoveStageFile(stagedPath,&stageIdentity);
        return false;
      }
      break;
    }
    const DWORD error = GetLastError();
    if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
      StoreFailure(errorDetails,
                   L"The staged configuration could not be reserved", error);
      return false;
    }
    stagedPath.clear();
  }
  if (stagedPath.empty()) {
    StoreFailure(errorDetails,
                 L"A unique staged configuration could not be reserved",
                 ERROR_FILE_EXISTS);
    return false;
  }

  if (!expected.missing &&
      !CopyFileW(configPath.c_str(), stagedPath.c_str(), FALSE)) {
    const DWORD error = GetLastError();
    RemoveStageFile(stagedPath,&stageIdentity);
    StoreFailure(errorDetails,
                 L"The existing configuration could not be staged", error);
    return false;
  }
  if (!SetFileAttributesW(stagedPath.c_str(), FILE_ATTRIBUTE_NORMAL)) {
    const DWORD error = GetLastError();
    RemoveStageFile(stagedPath,&stageIdentity);
    StoreFailure(errorDetails,
                 L"The staged configuration attributes could not be prepared",
                 error);
    return false;
  }
  ConfigSnapshot copied;
  if (!CaptureConfigSnapshot(stagedPath, copied, errorDetails, maximumBytes) ||
      copied.missing || copied.bytes != expected.bytes) {
    copied.Close();RemoveStageFile(stagedPath,&stageIdentity);
    if(errorDetails)*errorDetails=L"The source changed while it was copied. Existing file bytes were kept.";
    return false;
  }
  copied.Close();
#ifdef CTSPACES_CONFIG_TEST_HOOKS
  if(publicationHook)publicationHook(configPath,stagedPath,IniPublicationBoundary::AfterStageCopy,publicationHookUser);
#endif
  if (normalizeIni && !PrepareUnicodeIniStage(stagedPath, errorDetails)) {
    RemoveStageFile(stagedPath,&stageIdentity);
    return false;
  }

  bool mutated = false;
  std::wstring localMutationError;
  std::wstring &mutationError =
      errorDetails ? *errorDetails : localMutationError;
  try {
    mutated = mutation(stagedPath, mutationError);
  } catch (const std::exception &) {
    mutationError =
        L"The staged configuration update threw a standard exception.";
  } catch (...) {
    mutationError = L"The staged configuration update failed unexpectedly.";
  }
  if (!mutated) {
    if (mutationError.empty())
      mutationError = L"The staged configuration update was rejected.";
    RemoveStageFile(stagedPath,&stageIdentity);
    return false;
  }

  // Windows documents the all-null form as the profile-cache flush request,
  // but it returns FALSE on supported Windows builds even when the flush was
  // performed. Every actual mutation above is checked, and the file-system
  // flush immediately below is authoritative and checked as well.
  if (normalizeIni)
    (void)WritePrivateProfileStringW(nullptr, nullptr, nullptr,
                                     stagedPath.c_str());
  if (!FlushRegularFile(stagedPath, errorDetails)) {
    RemoveStageFile(stagedPath,&stageIdentity);
    return false;
  }

  ConfigSnapshot current;
  DWORD finalParentError = ERROR_SUCCESS;
  if (!IsSafeExistingDirectory(parentPath, finalParentError)) {
    RemoveStageFile(stagedPath,&stageIdentity);
    StoreFailure(errorDetails, L"The configuration folder changed during this update", finalParentError);
    return false;
  }
  if (!CaptureConfigSnapshot(configPath, current, errorDetails, maximumBytes)) {
    RemoveStageFile(stagedPath,&stageIdentity);
    return false;
  }
  if (!SameConfigSnapshot(expected, current)) {
    RemoveStageFile(stagedPath,&stageIdentity);
    if (errorDetails)
      *errorDetails = L"The configuration changed during this update. The newer file was kept; retry the setting change.";
    return false;
  }
  ConfigSnapshot staged;
  if(!CaptureConfigSnapshot(stagedPath,staged,errorDetails,maximumBytes) || staged.missing ||
     staged.identity.dwVolumeSerialNumber!=stageIdentity.dwVolumeSerialNumber ||
     staged.identity.nFileIndexHigh!=stageIdentity.nFileIndexHigh || staged.identity.nFileIndexLow!=stageIdentity.nFileIndexLow) {
    staged.Close();RemoveStageFile(stagedPath,&stageIdentity);
    if(errorDetails)*errorDetails=L"The staged configuration object changed during this update. The original configuration was kept.";
    return false;
  }
  if(expected.missing) {
#ifdef CTSPACES_CONFIG_TEST_HOOKS
    if(publicationHook)publicationHook(configPath,stagedPath,IniPublicationBoundary::BeforeNativePublication,publicationHookUser);
#endif
    if(MoveFileExW(stagedPath.c_str(),configPath.c_str(),MOVEFILE_WRITE_THROUGH)) {
      ConfigSnapshot installed;
      if(CaptureConfigSnapshot(configPath,installed,errorDetails,maximumBytes) && SameConfigSnapshot(staged,installed))return true;
      if(errorDetails)*errorDetails=L"The staged configuration changed during creation. Competing data was retained at "+configPath.wstring()+L".";
      return false;
    }
    const DWORD error = GetLastError();
    RemoveStageFile(stagedPath,&stageIdentity);
    StoreFailure(errorDetails,
                 L"The staged configuration could not replace config.ini",
                 error);
    return false;
  }

  // ReplaceFile can publish while the final read handle denies in-place
  // writes. Retain every displaced object as well: sharing rules cannot stop
  // every same-user rename primitive, so a replaced newer file must be kept.
  std::filesystem::path recoveryDirectory;
  bool recoveryReserved=false;
  for(unsigned attempt=0;attempt<32;++attempt) {
    if(!NewConfigStageName(configPath,attempt,recoveryDirectory,errorDetails)){
      staged.Close();RemoveStageFile(stagedPath,&stageIdentity);return false;
    }
    if(_wcsicmp(recoveryDirectory.filename().c_str(),configPath.filename().c_str())==0 ||
       _wcsicmp(recoveryDirectory.filename().c_str(),stagedPath.filename().c_str())==0){recoveryDirectory.clear();continue;}
    if(CreateDirectoryW(NativeConfigPath(recoveryDirectory).c_str(),nullptr)){recoveryReserved=true;break;}
    const DWORD error=GetLastError();recoveryDirectory.clear();
    if(error!=ERROR_ALREADY_EXISTS){staged.Close();RemoveStageFile(stagedPath,&stageIdentity);StoreFailure(errorDetails,L"Configuration recovery could not be reserved",error);return false;}
  }
  if(!recoveryReserved){staged.Close();RemoveStageFile(stagedPath,&stageIdentity);StoreFailure(errorDetails,L"Configuration recovery could not be reserved",ERROR_ALREADY_EXISTS);return false;}
  ConfigDirectoryLease recoveryLease;
  recoveryLease.file=CreateFileW(NativeConfigPath(recoveryDirectory).c_str(),GENERIC_READ,
      FILE_SHARE_READ,nullptr,OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
  BY_HANDLE_FILE_INFORMATION recoveryIdentity{};
  if(recoveryLease.file==INVALID_HANDLE_VALUE ||
     !GetFileInformationByHandle(recoveryLease.file,&recoveryIdentity) ||
     !(recoveryIdentity.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) ||
     (recoveryIdentity.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) {
    const DWORD error=GetLastError();staged.Close();RemoveStageFile(stagedPath,&stageIdentity);
    StoreFailure(errorDetails,L"The configuration recovery directory could not be protected",error);return false;
  }
  ConfigDirectoryLease recoverySentinel;
  if(!ProtectConfigDirectoryContents(recoveryDirectory/L"previous",recoverySentinel,errorDetails)) {
    staged.Close();RemoveStageFile(stagedPath,&stageIdentity);return false;
  }
  if(!AllowConfigDirectoryWrites(recoveryDirectory,recoveryLease,errorDetails)){
    staged.Close();RemoveStageFile(stagedPath,&stageIdentity);return false;
  }
  const auto displaced=NativeConfigPath(recoveryDirectory/L"previous");
  const auto rollbackDisplaced=NativeConfigPath(recoveryDirectory/L"rollback");
  const auto cleanupRecovery=[&]() {
    if(recoverySentinel.file!=INVALID_HANDLE_VALUE){CloseHandle(recoverySentinel.file);recoverySentinel.file=INVALID_HANDLE_VALUE;}
    CloseHandle(recoveryLease.file);recoveryLease.file=INVALID_HANDLE_VALUE;
    ConfigDirectoryLease bound;
    bound.file=CreateFileW(NativeConfigPath(recoveryDirectory).c_str(),DELETE|FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    BY_HANDLE_FILE_INFORMATION identity{};
    if(bound.file==INVALID_HANDLE_VALUE || !GetFileInformationByHandle(bound.file,&identity) ||
       identity.dwVolumeSerialNumber!=recoveryIdentity.dwVolumeSerialNumber ||
       identity.nFileIndexHigh!=recoveryIdentity.nFileIndexHigh || identity.nFileIndexLow!=recoveryIdentity.nFileIndexLow ||
       !(identity.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || (identity.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))return;
    FILE_DISPOSITION_INFO disposition{TRUE};
    SetFileInformationByHandle(bound.file,FileDispositionInfo,&disposition,sizeof(disposition));
  };
#ifdef CTSPACES_CONFIG_TEST_HOOKS
  if(publicationHook)publicationHook(configPath,displaced,IniPublicationBoundary::BeforePublish,publicationHookUser);
#endif
  ConfigSnapshot sourceNow;
  if(!CaptureConfigSnapshot(stagedPath,sourceNow,errorDetails,maximumBytes) ||
     !SameConfigSnapshot(staged,sourceNow)) {
    sourceNow.Close();staged.Close();RemoveStageFile(stagedPath,&stageIdentity);cleanupRecovery();
    if(errorDetails)*errorDetails=L"The staged configuration changed before publication. Original configuration bytes were kept.";
    return false;
  }
  sourceNow.Close();
  if(GetFileAttributesW(displaced.c_str())!=INVALID_FILE_ATTRIBUTES ||
     !IsMissingError(GetLastError())) {
    staged.Close();RemoveStageFile(stagedPath,&stageIdentity);
    if(errorDetails)*errorDetails=L"The configuration recovery location changed; its contents were retained at "+recoveryDirectory.wstring()+L".";
    return false;
  }
  // ReplaceFile opens the replacement for metadata merging. Retain its
  // captured identity/bytes, but close this read lock for that native call.
  staged.Close();
#ifdef CTSPACES_CONFIG_TEST_HOOKS
  if(publicationHook)publicationHook(configPath,stagedPath,IniPublicationBoundary::BeforeNativePublication,publicationHookUser);
#endif
  const bool published=PublishConfigReplacement(configPath,stagedPath,displaced,current)!=FALSE;
  const DWORD publicationError=published?ERROR_SUCCESS:GetLastError();
#ifdef CTSPACES_CONFIG_TEST_HOOKS
  if(publicationHook)publicationHook(configPath,displaced,IniPublicationBoundary::AfterPublish,publicationHookUser);
#endif
  ConfigSnapshot previous,installed;
  const bool previousRead=CaptureConfigSnapshot(displaced,previous,errorDetails,maximumBytes);
  const bool installedRead=CaptureConfigSnapshot(configPath,installed,errorDetails,maximumBytes);
  if(published && previousRead && installedRead && SameConfigSnapshot(expected,previous) &&
     SameConfigSnapshot(staged,installed)) {
    previous.Close();current.Close();staged.Close();installed.Close();
    DeleteMatchingConfigSnapshot(displaced,expected);cleanupRecovery();return true;
  }
  if(!published && previousRead && previous.missing && installedRead && SameConfigSnapshot(expected,installed)) {
    installed.Close();current.Close();DeleteMatchingConfigSnapshot(stagedPath,staged);cleanupRecovery();
    StoreFailure(errorDetails,L"The staged configuration could not replace config.ini",publicationError);return false;
  }
  // Its publication guard is no longer needed. In a partial native failure
  // this original object may now be the replacement source for recovery;
  // keeping the old read lock would deny ReplaceFile's metadata access.
  current.Close();
  // Only restore the displaced file if our exact stage is still installed.
  // A second replacement during rollback is itself retained separately.
  bool restored=false,ownedRollback=false;
#ifdef CTSPACES_CONFIG_TEST_HOOKS
  if(publicationHook)publicationHook(configPath,rollbackDisplaced,IniPublicationBoundary::BeforeRollback,publicationHookUser);
#endif
  if(previousRead && !previous.missing && installedRead && installed.missing) {
    previous.Close();
    restored=RenameMatchingConfigSnapshot(displaced,previous,configPath);
  } else if(previousRead && !previous.missing && installedRead && SameConfigSnapshot(staged,installed) &&
     GetFileAttributesW(rollbackDisplaced.c_str())==INVALID_FILE_ATTRIBUTES && IsMissingError(GetLastError())) {
    previous.Close();
    restored=ReplaceFileW(configPath.c_str(),displaced.c_str(),rollbackDisplaced.c_str(),
                           REPLACEFILE_IGNORE_MERGE_ERRORS,nullptr,nullptr)!=FALSE;
    if(!restored) {
      ConfigSnapshot now;
      if(CaptureConfigSnapshot(configPath,now,nullptr,maximumBytes) && now.missing)
        restored=RenameMatchingConfigSnapshot(displaced,previous,configPath);
    }
    if(restored) {
      ConfigSnapshot removed;
      ownedRollback=CaptureConfigSnapshot(rollbackDisplaced,removed,nullptr,maximumBytes) && SameConfigSnapshot(staged,removed);
    }
  }
  previous.Close();installed.Close();staged.Close();current.Close();
  if(ownedRollback && published)DeleteMatchingConfigSnapshot(rollbackDisplaced,staged);
  if(!published) {
    const auto pending=NativeConfigPath(recoveryDirectory/L"pending");
    (void)RenameMatchingConfigSnapshot(stagedPath,staged,pending);
  }
  if(restored && ownedRollback && published)cleanupRecovery();
  if(errorDetails) {
    if(!published) {
      StoreFailure(errorDetails,L"The atomic configuration publication failed",publicationError);
      *errorDetails+=restored?L" The displaced configuration was restored.":L" Available configurations were retained for recovery.";
    } else *errorDetails=restored?L"The configuration changed during publication. The newer file was restored; retry the setting change.":
                          L"The configuration changed during publication. Recovery data was retained; retry after reviewing the competing files.";
    if(GetFileAttributesW(recoveryDirectory.c_str())!=INVALID_FILE_ATTRIBUTES)
      *errorDetails+=L" Recovery: "+recoveryDirectory.wstring()+L".";
  }
  return false;
}

bool MutateIniFileAtomically(const std::filesystem::path &configPath,
                             const IniStageMutation &mutation,
                             std::wstring *errorDetails) {
  return MutateFileAtomically(configPath, mutation, true,
                             kMaximumIniMigrationBytes, true, errorDetails);
}

bool MutateRawFileAtomically(const std::filesystem::path &path,
                             const IniStageMutation &mutation,
                             size_t maximumBytes, bool allowCreate,
                             std::wstring *errorDetails) {
  return MutateFileAtomically(path, mutation, false,
                             maximumBytes, allowCreate, errorDetails);
}

bool ApplyIniMutationsAtomically(
    const std::filesystem::path &configPath,
    const std::vector<IniMutation> &mutations,
    std::wstring *errorDetails) {
  if (mutations.empty()) {
    if (errorDetails)
      *errorDetails = L"No configuration changes were supplied.";
    return false;
  }

  for (const auto &mutation : mutations) {
    if (mutation.section.empty() ||
        (!mutation.key && mutation.value) ||
        (mutation.key && mutation.key->empty())) {
      if (errorDetails)
        *errorDetails = L"An invalid INI mutation was supplied.";
      return false;
    }
  }

  return MutateIniFileAtomically(
      configPath,
      [&mutations](const std::filesystem::path &stagedPath,
                   std::wstring &mutationError) {
        for (const auto &mutation : mutations) {
          const wchar_t *key = mutation.key ? mutation.key->c_str() : nullptr;
          const wchar_t *value =
              mutation.value ? mutation.value->c_str() : nullptr;
          SetLastError(ERROR_SUCCESS);
          if (!WritePrivateProfileStringW(mutation.section.c_str(), key, value,
                                          stagedPath.c_str())) {
            const DWORD error = GetLastError();
            mutationError = std::format(
                L"Windows rejected an INI update for section [{}] "
                L"(Windows error {}).",
                mutation.section, error ? error : ERROR_WRITE_FAULT);
            return false;
          }
        }
        return true;
      },
      errorDetails);
}

DirectChildDirectoryState ProbeDirectChildDirectory(
    const std::filesystem::path &parentPath, const std::wstring &childName,
    std::wstring &resolvedName, unsigned long *windowsError) {
  resolvedName.clear();
  StoreWindowsError(windowsError, ERROR_SUCCESS);
  if (parentPath.empty() || childName.empty() || childName == L"." ||
      childName == L".." || childName.find_first_of(L"\\/:*?\"<>|") !=
                                   std::wstring::npos) {
    return DirectChildDirectoryState::MissingOrInvalid;
  }

  const DWORD parentAttributes = GetFileAttributesW(parentPath.c_str());
  if (parentAttributes == INVALID_FILE_ATTRIBUTES) {
    const DWORD error = GetLastError();
    StoreWindowsError(windowsError, error);
    return IsMissingError(error)
               ? DirectChildDirectoryState::MissingOrInvalid
               : DirectChildDirectoryState::Indeterminate;
  }
  if ((parentAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
      (parentAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    StoreWindowsError(windowsError, ERROR_ACCESS_DENIED);
    return DirectChildDirectoryState::Indeterminate;
  }

  const std::filesystem::path candidate = parentPath / childName;
  const DWORD attributes = GetFileAttributesW(candidate.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    const DWORD error = GetLastError();
    StoreWindowsError(windowsError, error);
    return IsMissingError(error)
               ? DirectChildDirectoryState::MissingOrInvalid
               : DirectChildDirectoryState::Indeterminate;
  }
  if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
      (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    return DirectChildDirectoryState::MissingOrInvalid;
  }

  WIN32_FIND_DATAW data{};
  HANDLE find = FindFirstFileW(candidate.c_str(), &data);
  if (find == INVALID_HANDLE_VALUE) {
    StoreWindowsError(windowsError, GetLastError());
    return DirectChildDirectoryState::Indeterminate;
  }
  FindClose(find);
  if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
      (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
      !data.cFileName[0]) {
    return DirectChildDirectoryState::MissingOrInvalid;
  }

  resolvedName = data.cFileName;
  return DirectChildDirectoryState::Present;
}

} // namespace config_persistence
