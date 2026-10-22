#pragma once
#include <windows.h>
#include <winternl.h>
#include "ClientDeletionSafety.h"
#include <filesystem>
#include <limits>
#include <memory>

namespace transient_profile {
#ifdef CTSPACES_TRANSIENT_RESERVATION_TEST_HOOKS
void BeforeReservation(const std::filesystem::path &path);
void AfterReservation(const std::filesystem::path &path, HANDLE directory);
#endif
// Retain the directory created for this launch, independently of its pathname.
// Copies of active-session bookkeeping share this owner but never use its handle.
class Identity {
  HANDLE directory_ = INVALID_HANDLE_VALUE;
  HANDLE reservation_ = INVALID_HANDLE_VALUE;
public:
  Identity() = default;
  Identity(const Identity &) = delete;
  Identity &operator=(const Identity &) = delete;
  ~Identity() { Release(); }
  // Win32 CreateDirectory followed by an open leaves a gap in which another
  // directory can replace the one we created. Native FILE_CREATE returns the
  // new directory and its original handle in one operation. A held reservation
  // keeps the directory nonempty against junction conversion while allowing
  // the native archive/preference writers' required directory write sharing.
  static std::shared_ptr<Identity> Create(const std::filesystem::path &path) {
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    const auto leaf = absolute.filename().native();
    if (leaf.empty() || leaf == L"." || leaf == L".." ||
        leaf.find_first_of(L"\\/:") != std::wstring::npos ||
        leaf.size() > ((std::numeric_limits<USHORT>::max)() / sizeof(wchar_t)) - 1) {
      SetLastError(ERROR_INVALID_NAME);return {};
    }
    std::wstring details;
    client_deletion_safety::BoundTree parent;
    if (!parent.CaptureNamespace(absolute.parent_path(), details) ||
        !parent.PinAncestor(absolute.parent_path(), false, details) ||
        !parent.ProtectEmptyParent(absolute.parent_path(), details)) return {};
    HANDLE parentHandle = CreateFileW(absolute.parent_path().c_str(),
        FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (parentHandle == INVALID_HANDLE_VALUE) return {};
    struct CloseParent { HANDLE value;~CloseParent(){CloseHandle(value);} } closeParent{parentHandle};
    const auto module = GetModuleHandleW(L"ntdll.dll");
    const auto create = reinterpret_cast<decltype(&NtCreateFile)>(GetProcAddress(module, "NtCreateFile"));
    const auto statusError = reinterpret_cast<decltype(&RtlNtStatusToDosError)>(GetProcAddress(module, "RtlNtStatusToDosError"));
    if (!create || !statusError || !parent.Verify(details)) {
      SetLastError(ERROR_NOT_SUPPORTED);return {};
    }
#ifdef CTSPACES_TRANSIENT_RESERVATION_TEST_HOOKS
    BeforeReservation(absolute);
#endif
    UNICODE_STRING name{};
    name.Buffer = const_cast<PWSTR>(leaf.c_str());
    name.Length = static_cast<USHORT>(leaf.size() * sizeof(wchar_t));
    name.MaximumLength = static_cast<USHORT>((leaf.size() + 1) * sizeof(wchar_t));
    OBJECT_ATTRIBUTES attributes{};attributes.Length = sizeof(attributes);
    attributes.RootDirectory = parentHandle;attributes.ObjectName = &name;
    attributes.Attributes = OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE;
    IO_STATUS_BLOCK result{};
    auto identity = std::make_shared<Identity>();
    const NTSTATUS status = create(&identity->directory_,
        FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY | SYNCHRONIZE, &attributes,
        &result, nullptr, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ,
        FILE_CREATE, FILE_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT |
                         FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0);
    if (status < 0) { SetLastError(statusError(status));return {}; }
    BY_HANDLE_FILE_INFORMATION information{};
    if (result.Information != FILE_CREATED ||
        !GetFileInformationByHandle(identity->directory_, &information) ||
        !(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
      SetLastError(ERROR_FILE_INVALID);return {};
    }
    identity->reservation_ = CreateFileW((absolute / L"~ctSpaces-reservation").c_str(),
        GENERIC_READ | DELETE, FILE_SHARE_READ, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE |
            FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (identity->reservation_ == INVALID_HANDLE_VALUE) return {};
    HANDLE compatible = CreateFileW(absolute.c_str(),
        FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (compatible == INVALID_HANDLE_VALUE) return {};
    if (!identity->Matches(compatible)) {
      CloseHandle(compatible);SetLastError(ERROR_FILE_INVALID);return {};
    }
    CloseHandle(identity->directory_);identity->directory_ = compatible;
#ifdef CTSPACES_TRANSIENT_RESERVATION_TEST_HOOKS
    AfterReservation(absolute, identity->directory_);
#endif
    if (!identity->MatchesPath(absolute)) { SetLastError(ERROR_FILE_INVALID);return {}; }
    return identity;
  }
  static std::shared_ptr<Identity> Capture(const std::filesystem::path &path) {
    auto result = std::make_shared<Identity>();
    result->directory_ = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION information{};
    if (result->directory_ == INVALID_HANDLE_VALUE ||
        !GetFileInformationByHandle(result->directory_, &information) ||
        !(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
      return {};
    return result;
  }
  bool Matches(HANDLE observed) const {
    BY_HANDLE_FILE_INFORMATION expected{}, actual{};
    return directory_ != INVALID_HANDLE_VALUE &&
        GetFileInformationByHandle(directory_, &expected) &&
        GetFileInformationByHandle(observed, &actual) &&
        !(actual.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        (actual.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
        expected.dwVolumeSerialNumber == actual.dwVolumeSerialNumber &&
        expected.nFileIndexHigh == actual.nFileIndexHigh &&
        expected.nFileIndexLow == actual.nFileIndexLow;
  }
  bool MatchesPath(const std::filesystem::path &path) const {
    HANDLE observed = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (observed == INVALID_HANDLE_VALUE)
      return false;
    const bool matches = Matches(observed);
    CloseHandle(observed);
    return matches;
  }
  // A freshly reserved profile may contain exactly our held protection file.
  // Its spelling alone never grants extraction authority over a nonempty tree.
  bool IsEmptyReservation(const std::filesystem::path &path) const {
    if (reservation_ == INVALID_HANDLE_VALUE || !MatchesPath(path)) return false;
    BY_HANDLE_FILE_INFORMATION expected{};
    if (!GetFileInformationByHandle(reservation_, &expected) ||
        (expected.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        expected.nNumberOfLinks != 1 || expected.nFileSizeHigh || expected.nFileSizeLow) return false;
    try {
      const std::filesystem::directory_iterator end;
      auto entry = std::filesystem::directory_iterator(path);
      if (entry == end || entry->path().filename() != L"~ctSpaces-reservation") return false;
      const auto filePath = entry->path();
      if (++entry != end) return false;
      HANDLE file = CreateFileW(filePath.c_str(), FILE_READ_ATTRIBUTES,
          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
          OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
      if (file == INVALID_HANDLE_VALUE) return false;
      BY_HANDLE_FILE_INFORMATION actual{};
      const bool same = GetFileInformationByHandle(file, &actual) &&
          !(actual.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
          actual.nNumberOfLinks == 1 && !actual.nFileSizeHigh && !actual.nFileSizeLow &&
          expected.dwVolumeSerialNumber == actual.dwVolumeSerialNumber &&
          expected.nFileIndexHigh == actual.nFileIndexHigh && expected.nFileIndexLow == actual.nFileIndexLow;
      CloseHandle(file);
      return same;
    } catch (...) { return false; }
  }
  // Only the launch/exit owner transfers this handle into bounded cleanup.
  // Other shared records only retain the object, so releasing the observer here
  // cannot leave a second native handle keeping a disposed root pending.
  void Release() {
    if (reservation_ != INVALID_HANDLE_VALUE) {
      CloseHandle(reservation_);
      reservation_ = INVALID_HANDLE_VALUE;
    }
    if (directory_ != INVALID_HANDLE_VALUE) {
      CloseHandle(directory_);
      directory_ = INVALID_HANDLE_VALUE;
    }
  }
};
}
