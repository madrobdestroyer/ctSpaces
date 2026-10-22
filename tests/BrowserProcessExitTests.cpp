#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../BrowserProcessExit.h"
#include <iostream>
#include <stdexcept>
#include <string>

static unsigned checks = 0;
static DWORD waitResult = WAIT_TIMEOUT;
static DWORD observedTimeout = MAXDWORD;
static HANDLE observedHandle = nullptr;
static DWORD WINAPI FakeWait(HANDLE process, DWORD timeout) {
  observedHandle = process;
  observedTimeout = timeout;
  return waitResult;
}
static void Check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
int wmain(int argc, wchar_t **argv) {
  if (argc == 2 && std::wstring(argv[1]) == L"--child") { Sleep(45); return 0; }
  try {
    using namespace browser_process_exit;
    HANDLE bound = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(1234));
    Check(!ConfirmExitAfterQueryFailure(nullptr, kProcessIsTerminating, FakeWait), "null handle never proves exit");
    Check(!ConfirmExitAfterQueryFailure(INVALID_HANDLE_VALUE, kProcessIsTerminating, FakeWait), "invalid handle never proves exit");
    const DWORD waitResults[] = {WAIT_OBJECT_0, WAIT_TIMEOUT, WAIT_FAILED, WAIT_ABANDONED};
    for (DWORD result : waitResults) {
      waitResult = result;
      Check(ConfirmExitAfterQueryFailure(bound, kProcessIsTerminating, FakeWait) == (result == WAIT_OBJECT_0), "teardown only admits signaled exact process");
      Check(observedHandle == bound && observedTimeout == 250, "teardown waits bounded interval on same object");
      Check(ConfirmExitAfterQueryFailure(bound, static_cast<LONG>(0xC0000022u), FakeWait) == (result == WAIT_OBJECT_0), "access denied only admits signaled exact process");
      Check(observedHandle == bound && observedTimeout == 0, "access errors do not delay or retry");
    }
    HANDLE self = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, GetCurrentProcessId());
    Check(self != nullptr, "live process test handle created");
    Check(!ConfirmExitAfterQueryFailure(self, static_cast<LONG>(0xC0000022u)), "uninspectable live process remains blocked");
    const ULONGLONG start = GetTickCount64();
    Check(!ConfirmExitAfterQueryFailure(self, kProcessIsTerminating), "unproven teardown remains blocked after bounded wait");
    Check(GetTickCount64() - start >= 200 && GetTickCount64() - start < 1500, "live process wait is bounded");
    CloseHandle(self);
    wchar_t exe[32768]{};
    Check(GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe))) > 0, "child executable located");
    for (int iteration = 0; iteration < 40; ++iteration) {
      std::wstring arguments = L"\"" + std::wstring(exe) + L"\" --child";
      STARTUPINFOW startup{sizeof(startup)};
      PROCESS_INFORMATION child{};
      Check(CreateProcessW(exe, arguments.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child) != FALSE, "short-lived test child started");
      CloseHandle(child.hThread);
      Check(ConfirmExitAfterQueryFailure(child.hProcess, kProcessIsTerminating), "same process handle proves child exit during teardown wait");
      Check(WaitForSingleObject(child.hProcess, 0) == WAIT_OBJECT_0, "admitted child actually exited");
      CloseHandle(child.hProcess);
    }
    std::cout << checks << " browser process exit checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
