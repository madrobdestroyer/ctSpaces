#pragma once
#include <windows.h>
#include <filesystem>

namespace archive_restore_boundary {
struct MoveFixture {
  std::filesystem::path stage, archive, held;
  BY_HANDLE_FILE_INFORMATION archiveIdentity{};
  bool attempted = false;
  bool moved = false;
};

inline thread_local MoveFixture *active = nullptr;

inline HANDLE WINAPI OpenFile(LPCWSTR path, DWORD access, DWORD sharing,
    LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD flags, HANDLE templateFile) {
  const HANDLE file = ::CreateFileW(path, access, sharing, security, creation, flags, templateFile);
  auto *fixture = active;
  if (!fixture || fixture->attempted || !path || file == INVALID_HANDLE_VALUE ||
      access != (GENERIC_READ | DELETE) || sharing != FILE_SHARE_READ || creation != OPEN_EXISTING ||
      flags != (FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT) ||
      _wcsicmp(path, fixture->stage.c_str()) != 0) return file;

  const DWORD originalError = ::GetLastError();
  fixture->attempted = true;
  BY_HANDLE_FILE_INFORMATION stage{};
  const bool ordinaryStage = ::GetFileInformationByHandle(file, &stage) &&
      (stage.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
      !(stage.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
  if (ordinaryStage) {
    const HANDLE archive = ::CreateFileW(fixture->archive.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (archive != INVALID_HANDLE_VALUE) {
      BY_HANDLE_FILE_INFORMATION current{};
      const bool exactArchive = ::GetFileInformationByHandle(archive, &current) &&
          !(current.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
          current.nNumberOfLinks == 1 && current.dwVolumeSerialNumber == fixture->archiveIdentity.dwVolumeSerialNumber &&
          current.nFileIndexHigh == fixture->archiveIdentity.nFileIndexHigh &&
          current.nFileIndexLow == fixture->archiveIdentity.nFileIndexLow;
      ::CloseHandle(archive);
      if (exactArchive)
        fixture->moved = ::MoveFileExW(fixture->archive.c_str(), fixture->held.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
    }
  }
  ::SetLastError(originalError);
  return file;
}
} // namespace archive_restore_boundary

#ifdef CTSPACES_ARCHIVE_FILESYSTEM_RESTORE_BOUNDARY
#define CreateFileW archive_restore_boundary::OpenFile
#endif
