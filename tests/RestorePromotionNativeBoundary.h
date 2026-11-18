#pragma once
#include <windows.h>
namespace restore_promotion_boundary {
BOOL WINAPI ReadFile(HANDLE,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
BOOL WINAPI SetFileInformationByHandle(HANDLE,FILE_INFO_BY_HANDLE_CLASS,LPVOID,DWORD);
}
// Applied only to the actual Backend.cpp translation unit in this console
// regression. Fixture I/O, SDK archive creation/extraction and CRC stay native.
#ifdef CTSPACES_RESTORE_PROMOTION_NATIVE_BOUNDARY
#define ReadFile restore_promotion_boundary::ReadFile
#define SetFileInformationByHandle restore_promotion_boundary::SetFileInformationByHandle
#endif