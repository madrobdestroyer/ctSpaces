#pragma once
#include <windows.h>

namespace backup_timestamp_boundary {
HANDLE WINAPI CreateFileW(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD,
                          DWORD, HANDLE);
BOOL WINAPI SetFileTime(HANDLE, const FILETIME*, const FILETIME*, const FILETIME*);
}

// This project applies the boundary only to the real InProc7z.cpp unit. The
// fixture and BackupEngine.cpp keep ordinary native Win32 calls.
#ifdef CTSPACES_TIMESTAMP_FAILURE_NATIVE_BOUNDARY
#define CreateFileW backup_timestamp_boundary::CreateFileW
#define SetFileTime backup_timestamp_boundary::SetFileTime
#endif
