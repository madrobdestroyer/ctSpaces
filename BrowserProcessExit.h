#pragma once
#include <windows.h>

namespace browser_process_exit {
inline constexpr LONG kProcessIsTerminating = static_cast<LONG>(0xC000010Au);
inline constexpr DWORD kTerminationWaitMilliseconds = 250;
using WaitFunction = DWORD(WINAPI *)(HANDLE, DWORD);

// Only a signaled handle proves that this exact process has exited. Wait a
// bounded interval for the specific NT teardown status, never for access errors
// or uninspectable live processes, and never reopen a potentially reused PID.
inline bool ConfirmExitAfterQueryFailure(HANDLE process, LONG status,
                                        WaitFunction wait = WaitForSingleObject) {
  if (!process || process == INVALID_HANDLE_VALUE) return false;
  const DWORD timeout = status == kProcessIsTerminating
      ? kTerminationWaitMilliseconds : 0;
  return wait(process, timeout) == WAIT_OBJECT_0;
}
} // namespace browser_process_exit
