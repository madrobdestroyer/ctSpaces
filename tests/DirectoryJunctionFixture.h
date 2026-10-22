#pragma once
#include <windows.h>
#include <winioctl.h>
#include <filesystem>
#include <vector>
#include <cstring>
#include <cstddef>

namespace CtTest {
// Test-only NTFS junctions require no symbolic-link privilege. Callers supply
// only their own disposable fixture paths and remove the link before cleanup.
inline bool CreateDirectoryJunction(const std::filesystem::path &link,
                                   const std::filesystem::path &target) {
  std::filesystem::create_directory(link);
  const std::wstring print = std::filesystem::absolute(target).wstring();
  const std::wstring substitute = L"\\??\\" + print;
  struct MountPoint {
    DWORD tag; WORD length, reserved;
    WORD substituteOffset, substituteLength, printOffset, printLength;
    wchar_t names[1];
  };
  const size_t namesBytes = (substitute.size() + print.size() + 2) * sizeof(wchar_t);
  std::vector<unsigned char> buffer(offsetof(MountPoint, names) + namesBytes, 0);
  auto *mount = reinterpret_cast<MountPoint *>(buffer.data());
  mount->tag = IO_REPARSE_TAG_MOUNT_POINT;
  mount->length = static_cast<WORD>(8 + namesBytes);
  mount->substituteLength = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
  mount->printOffset = static_cast<WORD>((substitute.size() + 1) * sizeof(wchar_t));
  mount->printLength = static_cast<WORD>(print.size() * sizeof(wchar_t));
  std::memcpy(mount->names, substitute.data(), mount->substituteLength);
  std::memcpy(reinterpret_cast<unsigned char *>(mount->names) + mount->printOffset,
              print.data(), mount->printLength);
  HANDLE handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;
  DWORD returned = 0;
  const bool success = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(),
      static_cast<DWORD>(buffer.size()), nullptr, 0, &returned, nullptr) != FALSE;
  CloseHandle(handle);
  return success;
}
}
