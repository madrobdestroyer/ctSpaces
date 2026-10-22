#pragma once

#include "ClientDeletionSafety.h"
#include "ConfigPersistence.h"
#include <windows.h>
#include <array>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

// This record belongs to the whole client, not to one browser slot. Moving or
// backing up that client therefore keeps its explicit tab-layout preference.
namespace client_tab_layout {
inline constexpr wchar_t kFileName[] = L"ctSpaces-client-settings";
inline constexpr size_t kMaximumBytes = 512;

enum class Layout { Browser = 0, Horizontal = 1, Vertical = 2 };
enum class Override { FollowGlobal = 0, Horizontal = 1, Vertical = 2 };

inline bool IsValid(Layout value) noexcept {
  return value == Layout::Browser || value == Layout::Horizontal ||
         value == Layout::Vertical;
}
inline bool IsValid(Override value) noexcept {
  return value == Override::FollowGlobal || value == Override::Horizontal ||
         value == Override::Vertical;
}
inline Layout Resolve(Layout global, Override client) noexcept {
  if (client == Override::Horizontal) return Layout::Horizontal;
  if (client == Override::Vertical) return Layout::Vertical;
  return IsValid(global) ? global : Layout::Browser;
}

#ifdef CTSPACES_CLIENT_TAB_LAYOUT_TEST_HOOKS
using BeforeMutationHook = void (*)(const std::filesystem::path &, void *);
inline thread_local BeforeMutationHook beforeMutationHook = nullptr;
inline thread_local void *beforeMutationUser = nullptr;
inline void SetBeforeMutationHookForTest(BeforeMutationHook hook,
                                          void *user = nullptr) {
  beforeMutationHook = hook;
  beforeMutationUser = user;
}
#endif

namespace detail {
namespace fs = std::filesystem;
inline std::mutex writeMutex;
inline constexpr std::string_view kPrefix = "ctSpaces-client-settings=1\r\n";

struct Handle {
  HANDLE value = INVALID_HANDLE_VALUE;
  explicit Handle(HANDLE file) noexcept : value(file) {}
  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;
  ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Snapshot {
  bool missing = false;
  BY_HANDLE_FILE_INFORMATION identity{};
  std::string bytes;
};
inline bool Fail(std::wstring *error, const wchar_t *message,
                  DWORD code = ERROR_INVALID_DATA) {
  if (error)
    *error = std::wstring(message) + L" (Windows error " +
             std::to_wstring(code ? code : ERROR_INVALID_DATA) + L").";
  return false;
}
inline bool RootPath(const fs::path &root, fs::path &absolute,
                      std::wstring *error) {
  if (root.empty() || !root.is_absolute())
    return Fail(error, L"The client settings require an absolute client folder",
                ERROR_INVALID_NAME);
  absolute = root.lexically_normal();
  if (absolute == absolute.root_path())
    return Fail(error, L"A drive root is not a client folder", ERROR_INVALID_NAME);
  return true;
}
inline bool SameIdentity(const BY_HANDLE_FILE_INFORMATION &left,
                          const BY_HANDLE_FILE_INFORMATION &right) noexcept {
  return left.dwVolumeSerialNumber == right.dwVolumeSerialNumber &&
         left.nFileIndexHigh == right.nFileIndexHigh &&
         left.nFileIndexLow == right.nFileIndexLow;
}
inline bool SafeInfo(HANDLE file, BY_HANDLE_FILE_INFORMATION &info,
                       std::wstring *error) {
  if (!GetFileInformationByHandle(file, &info))
    return Fail(error, L"The client settings file could not be inspected",
                GetLastError());
  if ((info.dwFileAttributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
      info.nNumberOfLinks != 1 || info.nFileSizeHigh != 0 ||
      info.nFileSizeLow > kMaximumBytes)
    return Fail(error, L"The client settings must be one bounded, safe regular file");
  return true;
}
inline bool ReadHandle(HANDLE file, Snapshot &snapshot, std::wstring *error) {
  if (!SafeInfo(file, snapshot.identity, error)) return false;
  std::array<char, kMaximumBytes> bytes{};
  LARGE_INTEGER start{};
  DWORD received = 0;
  if (!SetFilePointerEx(file, start, nullptr, FILE_BEGIN) ||
      !ReadFile(file, bytes.data(), snapshot.identity.nFileSizeLow,
                &received, nullptr) || received != snapshot.identity.nFileSizeLow)
    return Fail(error, L"The client settings file could not be read exactly",
                GetLastError());
  snapshot.missing = false;
  snapshot.bytes.assign(bytes.data(), received);
  return true;
}
inline bool ReadFileSnapshot(const fs::path &path, Snapshot &snapshot,
                               bool allowMissing, std::wstring *error) {
  const Handle file(CreateFileW(path.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (file.value == INVALID_HANDLE_VALUE) {
    const DWORD code = GetLastError();
    if (allowMissing && code == ERROR_FILE_NOT_FOUND) {
      snapshot = {};
      snapshot.missing = true;
      return true;
    }
    return Fail(error, L"The client settings file is unavailable", code);
  }
  return ReadHandle(file.value, snapshot, error);
}
inline bool SameSnapshot(const Snapshot &left, const Snapshot &right) noexcept {
  return left.missing || right.missing ? left.missing == right.missing
      : SameIdentity(left.identity, right.identity) && left.bytes == right.bytes;
}
inline std::string Encode(Override value) {
  const char *token = value == Override::Vertical ? "vertical" :
                      value == Override::Horizontal ? "horizontal" : "inherit";
  return std::string(kPrefix) + "tab-layout=" + token + "\r\n";
}
inline bool Decode(std::string_view bytes, Override &value) noexcept {
  // This is deliberately a closed, versioned format. Unknown future fields,
  // duplicate keys and alternate interpretations must not be overwritten by
  // an older application which understands only tab-layout.
  if (!bytes.starts_with(kPrefix)) return false;
  bytes.remove_prefix(kPrefix.size());
  if (bytes == "tab-layout=inherit\r\n") value = Override::FollowGlobal;
  else if (bytes == "tab-layout=horizontal\r\n") value = Override::Horizontal;
  else if (bytes == "tab-layout=vertical\r\n") value = Override::Vertical;
  else return false;
  return true;
}
inline bool WriteStage(const fs::path &path, const Snapshot &expected,
                         const std::string &bytes, std::wstring *error) {
  const Handle file(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH, nullptr));
  if (file.value == INVALID_HANDLE_VALUE)
    return Fail(error, L"The staged client settings could not be opened",
                GetLastError());
  Snapshot current;
  if (!ReadHandle(file.value, current, error) ||
      !SameSnapshot(expected, current))
    return Fail(error, L"The staged client settings changed before writing");
  LARGE_INTEGER beginning{};
  DWORD written = 0;
  if (!SetFilePointerEx(file.value, beginning, nullptr, FILE_BEGIN) ||
      !WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()),
                 &written, nullptr) || written != bytes.size() ||
      !SetEndOfFile(file.value) || !FlushFileBuffers(file.value))
    return Fail(error, L"The staged client settings could not be saved exactly",
                GetLastError());
  Snapshot verified;
  if (!ReadHandle(file.value, verified, error) ||
      !SameIdentity(current.identity, verified.identity) ||
      verified.bytes != bytes)
    return Fail(error, L"The staged client settings failed exact readback");
  return true;
}
} // namespace detail

// Missing settings mean FollowGlobal. A failed read leaves the output value
// untouched, allowing callers to distinguish corruption from an inherited choice.
inline bool Read(const std::filesystem::path &root, Override &value,
                  std::wstring *error = nullptr) {
  if (error) error->clear();
  try {
    std::filesystem::path absolute;
    if (!detail::RootPath(root, absolute, error)) return false;
    client_deletion_safety::BoundTree clientNamespace;
    std::wstring namespaceError;
    if (!clientNamespace.CaptureNamespace(absolute, namespaceError)) {
      if (error) *error = std::move(namespaceError);
      return false;
    }
    detail::Snapshot snapshot;
    if (!detail::ReadFileSnapshot(absolute / kFileName, snapshot, true, error))
      return false;
    Override parsed = Override::FollowGlobal;
    if (!snapshot.missing && !detail::Decode(snapshot.bytes, parsed))
      return detail::Fail(error, L"The client settings format is unknown or malformed");
    if (!clientNamespace.Verify(namespaceError)) {
      if (error) *error = std::move(namespaceError);
      return false;
    }
    value = parsed;
    return true;
  } catch (...) {
    return detail::Fail(error, L"The client settings could not be read safely");
  }
}

// Deletion retry authority requires an existing bounded safe regular file, not
// a decodable preference. A corrupt or newer version remains deletable as part
// of the explicitly selected client; Read/Write still refuse to interpret it.
// A caller which already owns the original namespace (including DELETE-access
// leases) may supply it. Verify that exact root before and after the leaf read;
// opening another independent namespace lease would conflict with its DELETE
// access. The caller retains that authority for the actual destructive action.
inline bool ValidateFile(const std::filesystem::path &path,
                          std::wstring *error = nullptr,
                          client_deletion_safety::BoundTree *existingNamespace = nullptr) {
  if (error) error->clear();
  try {
    if (CompareStringOrdinal(path.filename().c_str(), -1, kFileName, -1, TRUE) !=
        CSTR_EQUAL)
      return detail::Fail(error, L"This is not a client settings filename",
                          ERROR_INVALID_NAME);
    std::filesystem::path absolute;
    if (!detail::RootPath(path.parent_path(), absolute, error)) return false;
    client_deletion_safety::BoundTree clientNamespace;
    std::wstring namespaceError;
    if (existingNamespace && !existingNamespace->IsAt(absolute))
      return detail::Fail(error,
          L"The supplied namespace belongs to a different client settings folder",
          ERROR_FILE_INVALID);
    auto &authority = existingNamespace ? *existingNamespace : clientNamespace;
    if (!(existingNamespace ? authority.Verify(namespaceError)
                            : authority.CaptureNamespace(absolute, namespaceError))) {
      if (error) *error = std::move(namespaceError);
      return false;
    }
    detail::Snapshot snapshot;
    if (!detail::ReadFileSnapshot(absolute / kFileName, snapshot, false, error))
      return false;
    if (!authority.Verify(namespaceError)) {
      if (error) *error = std::move(namespaceError);
      return false;
    }
    return true;
  } catch (...) {
    return detail::Fail(error, L"The client settings file could not be validated safely");
  }
}

inline bool Write(const std::filesystem::path &root, Override value,
                   std::wstring *error = nullptr) {
  if (error) error->clear();
  if (!IsValid(value))
    return detail::Fail(error, L"The requested client tab layout is invalid",
                        ERROR_INVALID_PARAMETER);
  try {
    std::lock_guard<std::mutex> lock(detail::writeMutex);
    std::filesystem::path absolute;
    if (!detail::RootPath(root, absolute, error)) return false;
    client_deletion_safety::BoundTree clientNamespace;
    std::wstring namespaceError;
    if (!clientNamespace.CaptureNamespace(absolute, namespaceError)) {
      if (error) *error = std::move(namespaceError);
      return false;
    }
    const auto path = absolute / kFileName;
    detail::Snapshot original;
    if (!detail::ReadFileSnapshot(path, original, true, error)) return false;
    Override previous = Override::FollowGlobal;
    if (!original.missing && !detail::Decode(original.bytes, previous))
      return detail::Fail(error, L"Unknown or malformed client settings were kept unchanged");
    if (!original.missing &&
        (original.identity.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0)
      return detail::Fail(error, L"The client settings file is read-only",
                          ERROR_ACCESS_DENIED);
    const std::string contents = detail::Encode(value);
#ifdef CTSPACES_CLIENT_TAB_LAYOUT_TEST_HOOKS
    if (beforeMutationHook) beforeMutationHook(absolute, beforeMutationUser);
#endif
    if (!clientNamespace.Verify(namespaceError)) {
      if (error) *error = std::move(namespaceError);
      return false;
    }
    return config_persistence::MutateRawFileAtomically(path,
        [&](const std::filesystem::path &stage, std::wstring &stageError) {
          if (!clientNamespace.Verify(stageError)) return false;
          detail::Snapshot stillOriginal;
          if (!detail::ReadFileSnapshot(path, stillOriginal, true, &stageError))
            return false;
          if (!detail::SameSnapshot(original, stillOriginal))
            return detail::Fail(&stageError,
                L"The client settings changed before this update; the newer file was kept");
          detail::Snapshot staged;
          if (!detail::ReadFileSnapshot(stage, staged, false, &stageError))
            return false;
          if (staged.bytes != original.bytes)
            return detail::Fail(&stageError,
                L"The staged client settings do not match the original bytes");
          return detail::WriteStage(stage, staged, contents, &stageError);
        }, kMaximumBytes, true, error);
  } catch (...) {
    return detail::Fail(error, L"The client settings could not be written safely");
  }
}
} // namespace client_tab_layout
