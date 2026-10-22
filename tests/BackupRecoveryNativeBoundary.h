#pragma once
#include <windows.h>

namespace backup_recovery_boundary {
HANDLE WINAPI CreateFileW(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD,
                          DWORD, HANDLE);
BOOL WINAPI WriteFile(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
BOOL WINAPI ReadFile(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
BOOL WINAPI FlushFileBuffers(HANDLE);
BOOL WINAPI SetFileInformationByHandle(HANDLE, FILE_INFO_BY_HANDLE_CLASS,
                                     LPVOID, DWORD);
}

// Forced into the actual production SDK/backend translation units only in
// this console test project. No application UI or process entrypoint is linked.
#define CreateFileW backup_recovery_boundary::CreateFileW
#define WriteFile backup_recovery_boundary::WriteFile
#define ReadFile backup_recovery_boundary::ReadFile
#define FlushFileBuffers backup_recovery_boundary::FlushFileBuffers
#define SetFileInformationByHandle backup_recovery_boundary::SetFileInformationByHandle
