#pragma once
#include <windows.h>
namespace backup_exception_boundary {
BOOL WINAPI SetFileInformationByHandle(HANDLE, FILE_INFO_BY_HANDLE_CLASS,
                                      LPVOID, DWORD);
}
// Observe native backup disposition and publication in the backend. Source
// reads, compression, completed-archive CRC and extraction use the native SDK.
#ifdef CTSPACES_BACKUP_EXCEPTION_NATIVE_BOUNDARY
#define SetFileInformationByHandle backup_exception_boundary::SetFileInformationByHandle
#endif

namespace backup_source_boundary {
HANDLE WINAPI CreateFileW(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD,
                          DWORD, HANDLE);
BOOL WINAPI CloseHandle(HANDLE);
HANDLE WINAPI FindFirstFileW(LPCWSTR, LPWIN32_FIND_DATAW);
BOOL WINAPI FindClose(HANDLE);
}

// Observe the real native acquisitions and releases in the archive bridge.
// The harness arms one allocation failure after opening its exact source file.
#ifdef CTSPACES_BACKUP_SOURCE_NATIVE_BOUNDARY
#define CreateFileW backup_source_boundary::CreateFileW
#define CloseHandle backup_source_boundary::CloseHandle
#define FindFirstFileW backup_source_boundary::FindFirstFileW
#define FindClose backup_source_boundary::FindClose
#endif
