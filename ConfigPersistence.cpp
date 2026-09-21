#include "ConfigPersistence.h"

#include <windows.h>

#include <format>
#include <mutex>
#include <new>
#include <system_error>
#include <vector>

namespace config_persistence {
namespace {

std::mutex g_iniMutationMutex;

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

bool RemoveStageFile(const std::filesystem::path &path) {
  if (path.empty())
    return true;
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES)
    return IsMissingError(GetLastError());
  if ((attributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
    return false;
  }
  SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
  if (DeleteFileW(path.c_str()))
    return true;
  return IsMissingError(GetLastError());
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

bool MutateIniFileAtomically(const std::filesystem::path &configPath,
                             const IniStageMutation &mutation,
                             std::wstring *errorDetails) {
  if (errorDetails)
    errorDetails->clear();
  if (!mutation || configPath.empty() || !configPath.has_filename()) {
    StoreFailure(errorDetails, L"The configuration update is invalid",
                 ERROR_INVALID_PARAMETER);
    return false;
  }

  std::lock_guard<std::mutex> lock(g_iniMutationMutex);

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

  std::filesystem::path stagedPath;
  for (unsigned attempt = 0; attempt < 32; ++attempt) {
    stagedPath = parentPath /
                 std::format(L".{}.ctspaces-{}-{}-{}.tmp",
                             configPath.filename().wstring(),
                             GetCurrentProcessId(), GetTickCount64(), attempt);
    HANDLE reservation = CreateFileW(
        stagedPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH,
        nullptr);
    if (reservation != INVALID_HANDLE_VALUE) {
      if (!CloseHandle(reservation)) {
        StoreFailure(errorDetails,
                     L"The staged configuration could not be reserved",
                     GetLastError());
        RemoveStageFile(stagedPath);
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

  if (originalState == ConfigFileState::Regular &&
      !CopyFileW(configPath.c_str(), stagedPath.c_str(), FALSE)) {
    const DWORD error = GetLastError();
    RemoveStageFile(stagedPath);
    StoreFailure(errorDetails,
                 L"The existing configuration could not be staged", error);
    return false;
  }
  if (!SetFileAttributesW(stagedPath.c_str(), FILE_ATTRIBUTE_NORMAL)) {
    const DWORD error = GetLastError();
    RemoveStageFile(stagedPath);
    StoreFailure(errorDetails,
                 L"The staged configuration attributes could not be prepared",
                 error);
    return false;
  }
  if (!PrepareUnicodeIniStage(stagedPath, errorDetails)) {
    RemoveStageFile(stagedPath);
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
    RemoveStageFile(stagedPath);
    return false;
  }

  // Windows documents the all-null form as the profile-cache flush request,
  // but it returns FALSE on supported Windows builds even when the flush was
  // performed. Every actual mutation above is checked, and the file-system
  // flush immediately below is authoritative and checked as well.
  (void)WritePrivateProfileStringW(nullptr, nullptr, nullptr,
                                   stagedPath.c_str());
  if (!FlushRegularFile(stagedPath, errorDetails)) {
    RemoveStageFile(stagedPath);
    return false;
  }

  if (!MoveFileExW(stagedPath.c_str(), configPath.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    const DWORD error = GetLastError();
    RemoveStageFile(stagedPath);
    StoreFailure(errorDetails,
                 L"The staged configuration could not replace config.ini",
                 error);
    return false;
  }
  return true;
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
