#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace installer_publication_boundary {
HANDLE WINAPI CreateFileW(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD,
                          DWORD, HANDLE);
BOOL WINAPI MoveFileExW(LPCWSTR, LPCWSTR, DWORD);
BOOL WINAPI WriteFile(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
BOOL WINAPI FlushFileBuffers(HANDLE);
BOOL WINAPI SetFileInformationByHandle(HANDLE, FILE_INFO_BY_HANDLE_CLASS,
                                     LPVOID, DWORD);
BOOL WINAPI ShellExecuteExW(SHELLEXECUTEINFOW *);
}

// Exercise the production copy implementation with inert filesystem fixtures.
// No application entrypoint, dialog, installed child or registry API is invoked.
#define CTSPACES_INSTALLER_TEST_HOOKS
#define wWinMain CtSpacesPublicationUnusedWinMain
#define CreateFileW installer_publication_boundary::CreateFileW
#define MoveFileExW installer_publication_boundary::MoveFileExW
#define WriteFile installer_publication_boundary::WriteFile
#define FlushFileBuffers installer_publication_boundary::FlushFileBuffers
#define SetFileInformationByHandle installer_publication_boundary::SetFileInformationByHandle
#define ShellExecuteExW installer_publication_boundary::ShellExecuteExW
#include "../ctSpaces.cpp"
#undef ShellExecuteExW
#undef SetFileInformationByHandle
#undef FlushFileBuffers
#undef WriteFile
#undef MoveFileExW
#undef CreateFileW
#undef wWinMain

namespace {
enum class Fault { None, CreateCollision, ReplaceBeforeCommit, FailCreate,
                   FailWrite, FailFlush, FailRename, FailCleanup };
Fault g_fault = Fault::None;
bool g_mutated = false;
fs::path g_fixture;
fs::path g_stage;
fs::path g_foreign;
HANDLE g_ownedStage = INVALID_HANDLE_VALUE;
bool g_attackBlocked = false;
bool g_writeBlocked = false;
unsigned g_checks = 0;
unsigned g_forbidden = 0;
unsigned g_fixtureNumber = 0;
const std::string kForeign = "FOREIGN STAGING FILE - MUST SURVIVE";
const std::string kOld = "EXACT PREVIOUS INSTALLED FILE";

void Check(bool value, const char *text) {
  ++g_checks;
  if (!value)
    throw std::runtime_error(text);
}
void Write(const fs::path &path, const std::string &value) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(value.data(), static_cast<std::streamsize>(value.size()));
  if (!out)
    throw std::runtime_error("fixture write failed");
}
std::vector<char> Read(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return {};
  return {std::istreambuf_iterator<char>(in), {}};
}
bool Equals(const fs::path &path, const std::string &value) {
  return Read(path) == std::vector<char>(value.begin(), value.end());
}
bool IsStage(const fs::path &path) {
  return !g_fixture.empty() && path.parent_path() == g_fixture &&
         path.filename() != L"ctSpaces.exe";
}
void RunCase(Fault fault, bool expectFixed) {
  const auto root = fs::current_path() / L"build" /
      std::format(L"ip6-{}-{}-{}", GetCurrentProcessId(), GetTickCount64(),
                  ++g_fixtureNumber);
  Check(fs::create_directory(root), "unique fixture creation failed");
  g_fixture = root;
  g_sDataDir = root;
  g_stage.clear();
  g_foreign.clear();
  g_ownedStage = INVALID_HANDLE_VALUE;
  g_attackBlocked = false;
  g_writeBlocked = false;
  g_fault = fault;
  g_mutated = false;
  const auto installed = root / L"ctSpaces.exe";
  Write(installed, kOld);
  const auto current = GetCurrentExecutablePath();
  const auto sourceBefore = Read(current);
  std::wstring details;
  const bool copied = CopyFileWithRetry(current, installed, &details);
  const bool foreignRetained = !g_foreign.empty() && Equals(g_foreign, kForeign);
  const bool oldRetained = Equals(installed, kOld);
  const bool foreignInstalled = Equals(installed, kForeign);
  std::cout << "CASE=" << static_cast<int>(fault)
            << " MUTATION=" << g_mutated << " COPY_OK=" << copied
            << " FOREIGN_RETAINED=" << foreignRetained
            << " OLD_INSTALLED_RETAINED=" << oldRetained
            << " FOREIGN_INSTALLED=" << foreignInstalled << '\n';
  std::wcout << L"DETAILS=" << details << L"\nFIXTURE=" << root << L'\n';
  Check(Read(current) == sourceBefore, "running source bytes changed");
  Check(g_forbidden == 0, "unexpected dialog or launch boundary");
  Check(g_mutated, "requested boundary did not execute");
  if (expectFixed) {
    Check(!foreignInstalled, "unverified foreign stage was installed");
    if (fault == Fault::CreateCollision) {
      Check(foreignRetained, "foreign collision object was removed or moved");
      Check(copied ? Read(installed) == sourceBefore : oldRetained,
            "collision did not preserve the installed file or publish verified bytes");
    } else if (fault == Fault::ReplaceBeforeCommit) {
      Check(g_attackBlocked, "verified stage pathname was replaceable at commit");
      Check(g_writeBlocked, "verified stage contents were writable at commit");
      Check(copied && Read(installed) == sourceBefore,
            "blocked substitution did not install verified source bytes");
    } else {
      Check(!copied && oldRetained,
            "injected copy failure changed or replaced installed bytes");
      if (fault == Fault::FailCleanup) {
        Check(!g_stage.empty() && fs::is_regular_file(g_stage),
              "cleanup refusal did not retain the owned partial stage");
        Check(details.find(L"retained at " + g_stage.wstring()) != std::wstring::npos,
              "cleanup refusal omitted its exact retained stage path");
        const auto partial = Read(g_stage);
        Check(partial.size() == 17 &&
              std::equal(partial.begin(), partial.end(), sourceBefore.begin()),
              "cleanup refusal retained unexpected partial bytes");
      } else {
        Check(!g_stage.empty() && !fs::exists(g_stage),
              "failure left its owned staging object behind");
      }
    }
    Check(!details.empty() || copied, "rejection omitted its error details");
  } else {
    Check(!copied, "fault was incorrectly reported successful");
  }
  // Retain every disposable fixture as evidence, including successful rejects.
  g_fixture.clear();
  g_sDataDir.clear();
}

void RunSuccessfulCopy(bool existing) {
  const auto root = fs::current_path() / L"build" /
      std::format(L"ip6-{}-{}-{}", GetCurrentProcessId(), GetTickCount64(),
                  ++g_fixtureNumber);
  Check(fs::create_directory(root), "positive fixture creation failed");
  g_fixture = root;
  g_sDataDir = root;
  g_stage.clear(); g_foreign.clear(); g_ownedStage = INVALID_HANDLE_VALUE;
  g_fault = Fault::None;
  const auto installed = root / L"ctSpaces.exe";
  if (existing) Write(installed, kOld);
  const auto source = GetCurrentExecutablePath();
  std::wstring details;
  Check(CopyFileWithRetry(source, installed, &details),
        "ordinary fixture install/update failed");
  Check(Read(source) == Read(installed), "ordinary installed bytes differ");
  Check(!g_stage.empty() && !fs::exists(g_stage),
        "ordinary publication left a staging path");
  Check(g_forbidden == 0, "ordinary copy invoked a forbidden boundary");
  std::cout << "PASS ordinary " << (existing ? "replacement" : "first install") << '\n';
  g_fixture.clear(); g_sDataDir.clear();
}
} // namespace

int CtSpacesInstallerTestMessageBox(HWND, LPCWSTR, LPCWSTR, UINT) {
  ++g_forbidden;
  return IDCANCEL;
}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path &, std::wstring &) {
  return false;
}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path &) {}

namespace installer_publication_boundary {
HANDLE WINAPI CreateFileW(LPCWSTR name, DWORD access, DWORD sharing,
    LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD flags, HANDLE templ) {
  if (name && creation == CREATE_NEW && IsStage(fs::path(name))) {
    g_stage = name;
    if (g_fault == Fault::CreateCollision && !g_mutated) {
      Write(g_stage, kForeign);
      g_foreign = g_stage;
      g_mutated = true;
    }
    if (g_fault == Fault::FailCreate) {
      g_mutated = true;
      SetLastError(ERROR_ACCESS_DENIED);
      return INVALID_HANDLE_VALUE;
    }
  }
  HANDLE file = ::CreateFileW(name, access, sharing, security, creation, flags, templ);
  if (file != INVALID_HANDLE_VALUE && name && creation == CREATE_NEW &&
      IsStage(fs::path(name)))
    g_ownedStage = file;
  return file;
}
BOOL WINAPI MoveFileExW(LPCWSTR existing, LPCWSTR replacement, DWORD flags) {
  if (existing && IsStage(fs::path(existing)) &&
      g_fault == Fault::ReplaceBeforeCommit && !g_mutated) {
    g_stage = existing;
    const auto movedAside = g_stage.wstring() + L".original";
    Check(::MoveFileExW(existing, movedAside.c_str(), 0) != FALSE,
          "could not move validated staging object aside");
    Write(g_stage, kForeign);
    g_foreign = g_stage;
    g_mutated = true;
  }
  return ::MoveFileExW(existing, replacement, flags);
}
BOOL WINAPI WriteFile(HANDLE file, LPCVOID bytes, DWORD count, LPDWORD written,
                      LPOVERLAPPED overlapped) {
  if (file == g_ownedStage &&
      (g_fault == Fault::FailWrite || g_fault == Fault::FailCleanup) && !g_mutated) {
    const DWORD partial = (std::min)(count, DWORD{17});
    if (partial && !::WriteFile(file, bytes, partial, written, overlapped))
      return FALSE;
    g_mutated = true;
    SetLastError(ERROR_DISK_FULL);
    return FALSE;
  }
  return ::WriteFile(file, bytes, count, written, overlapped);
}
BOOL WINAPI FlushFileBuffers(HANDLE file) {
  if (file == g_ownedStage && g_fault == Fault::FailFlush) {
    g_mutated = true;
    SetLastError(ERROR_WRITE_FAULT);
    return FALSE;
  }
  return ::FlushFileBuffers(file);
}
BOOL WINAPI SetFileInformationByHandle(HANDLE file,
    FILE_INFO_BY_HANDLE_CLASS kind, LPVOID info, DWORD bytes) {
  if (file == g_ownedStage && kind == FileRenameInfo) {
    if (g_fault == Fault::ReplaceBeforeCommit && !g_mutated) {
      const auto movedAside = g_stage.wstring() + L".original";
      const bool moved = ::MoveFileExW(g_stage.c_str(), movedAside.c_str(), 0) != FALSE;
      g_attackBlocked = !moved;
      if (moved) { Write(g_stage, kForeign); g_foreign = g_stage; }
      HANDLE writer = ::CreateFileW(g_stage.c_str(), GENERIC_WRITE,
          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
          OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
      g_writeBlocked = writer == INVALID_HANDLE_VALUE;
      if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
      g_mutated = true;
    }
    if (g_fault == Fault::FailRename) {
      g_mutated = true;
      SetLastError(ERROR_DISK_FULL);
      return FALSE;
    }
  }
  if (file == g_ownedStage && kind == FileDispositionInfo &&
      g_fault == Fault::FailCleanup) {
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
  }
  return ::SetFileInformationByHandle(file, kind, info, bytes);
}
BOOL WINAPI ShellExecuteExW(SHELLEXECUTEINFOW *) {
  ++g_forbidden;
  SetLastError(ERROR_ACCESS_DENIED);
  return FALSE;
}
} // namespace installer_publication_boundary

int wmain(int argc, wchar_t **argv) {
  try {
    const bool original = argc == 2 && wcscmp(argv[1], L"--before-proof") == 0;
    Check(original || argc == 1, "unknown argument refused");
    RunCase(Fault::CreateCollision, !original);
    RunCase(Fault::ReplaceBeforeCommit, !original);
    if (!original) {
      RunCase(Fault::FailCreate, true);
      RunCase(Fault::FailWrite, true);
      RunCase(Fault::FailFlush, true);
      RunCase(Fault::FailRename, true);
      RunCase(Fault::FailCleanup, true);
      RunSuccessfulCopy(false);
      RunSuccessfulCopy(true);
    }
    std::cout << "Installer publication boundary checks passed: "
              << g_checks << ".\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Installer publication check failed: " << error.what() << '\n';
    return 1;
  }
}
