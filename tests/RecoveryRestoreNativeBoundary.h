#pragma once
#include <windows.h>
namespace recovery_restore_boundary {
BOOL WINAPI SetFileInformationByHandle(HANDLE, FILE_INFO_BY_HANDLE_CLASS, LPVOID, DWORD);
}
// This definition is supplied only for the actual BackupEngine.cpp translation
// unit. All ordinary SDK, fixture, and other application I/O remains native.
#ifdef CTSPACES_RECOVERY_RESTORE_BACKEND_BOUNDARY
#define SetFileInformationByHandle recovery_restore_boundary::SetFileInformationByHandle
#endif
