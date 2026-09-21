#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

class ScopedHandle {
public:
  ScopedHandle() = default;
  explicit ScopedHandle(HANDLE handle) : handle_(handle) {}
  ~ScopedHandle() { Reset(); }

  ScopedHandle(const ScopedHandle &) = delete;
  ScopedHandle &operator=(const ScopedHandle &) = delete;

  ScopedHandle(ScopedHandle &&other) noexcept : handle_(other.Release()) {}
  ScopedHandle &operator=(ScopedHandle &&other) noexcept {
    if (this != &other)
      Reset(other.Release());
    return *this;
  }

  HANDLE Get() const noexcept { return handle_; }
  explicit operator bool() const noexcept {
    return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
  }

  HANDLE Release() noexcept {
    HANDLE released = handle_;
    handle_ = INVALID_HANDLE_VALUE;
    return released;
  }

  void Reset(HANDLE replacement = INVALID_HANDLE_VALUE) noexcept {
    if (handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr)
      CloseHandle(handle_);
    handle_ = replacement;
  }

private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
};

struct FileIdentity {
  DWORD volumeSerialNumber = 0;
  DWORD fileIndexHigh = 0;
  DWORD fileIndexLow = 0;
};

bool operator==(const FileIdentity &left, const FileIdentity &right) {
  return left.volumeSerialNumber == right.volumeSerialNumber &&
         left.fileIndexHigh == right.fileIndexHigh &&
         left.fileIndexLow == right.fileIndexLow;
}

bool operator!=(const FileIdentity &left, const FileIdentity &right) {
  return !(left == right);
}

void ReportIdentity(const wchar_t *label, const FileIdentity &identity) {
  std::wcerr << label << L": volume=" << identity.volumeSerialNumber
             << L", index=" << identity.fileIndexHigh << L":"
             << identity.fileIndexLow << L".\n";
}

void ReportDirectoryEntries(const fs::path &root) {
  std::error_code error;
  std::wcerr << L"Directory entries after rename:";
  for (const fs::directory_entry &entry :
       fs::directory_iterator(root, error)) {
    std::wcerr << L" [" << entry.path().filename().wstring() << L"]";
  }
  if (error)
    std::wcerr << L" (enumeration error " << error.value() << L")";
  std::wcerr << L".\n";
}

void ReportWindowsError(const wchar_t *operation, DWORD error) {
  std::wcerr << operation << L" failed with Windows error " << error
             << L".\n";
}

bool Require(bool condition, const wchar_t *message) {
  if (!condition)
    std::wcerr << message << L'\n';
  return condition;
}

bool GetIdentity(HANDLE handle, FileIdentity &identity) {
  BY_HANDLE_FILE_INFORMATION information{};
  if (!GetFileInformationByHandle(handle, &information))
    return false;
  identity.volumeSerialNumber = information.dwVolumeSerialNumber;
  identity.fileIndexHigh = information.nFileIndexHigh;
  identity.fileIndexLow = information.nFileIndexLow;
  return true;
}

bool GetPathIdentity(const fs::path &path, FileIdentity &identity) {
  ScopedHandle handle(CreateFileW(
      path.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr));
  return handle && GetIdentity(handle.Get(), identity);
}

bool PathIsMissing(const fs::path &path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES)
    return false;
  const DWORD error = GetLastError();
  return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

bool WriteNewFile(const fs::path &path, const std::string &contents) {
  ScopedHandle handle(CreateFileW(
      path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr));
  if (!handle) {
    ReportWindowsError(L"CreateFileW(CREATE_NEW)", GetLastError());
    return false;
  }
  DWORD written = 0;
  if (!WriteFile(handle.Get(), contents.data(),
                 static_cast<DWORD>(contents.size()), &written, nullptr) ||
      written != contents.size() || !FlushFileBuffers(handle.Get())) {
    ReportWindowsError(L"WriteFile/FlushFileBuffers", GetLastError());
    return false;
  }
  return true;
}

bool ReadFileContents(const fs::path &path, std::string &contents) {
  ScopedHandle handle(CreateFileW(
      path.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr));
  if (!handle)
    return false;

  LARGE_INTEGER size{};
  if (!GetFileSizeEx(handle.Get(), &size) || size.QuadPart < 0 ||
      size.QuadPart > 1024)
    return false;
  contents.assign(static_cast<size_t>(size.QuadPart), '\0');
  DWORD bytesRead = 0;
  if (!contents.empty() &&
      (!ReadFile(handle.Get(), contents.data(),
                 static_cast<DWORD>(contents.size()), &bytesRead, nullptr) ||
       bytesRead != contents.size())) {
    return false;
  }
  return true;
}

ScopedHandle OpenForHandleRename(const fs::path &path, DWORD shareMode) {
  return ScopedHandle(CreateFileW(
      path.c_str(), DELETE | FILE_READ_ATTRIBUTES, shareMode, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr));
}

bool RenameOpenedFileNoReplace(HANDLE fileHandle,
                               const fs::path &destination,
                               FileIdentity *reportedIdentity,
                               DWORD &renameError) {
  const std::wstring destinationText = destination.wstring();
  const size_t fileNameBytes =
      destinationText.size() * sizeof(destinationText.front());
  // FileNameLength excludes the terminator, but the backing buffer includes
  // a zero WCHAR. Omitting it can produce a successful rename to a filename
  // with a garbage trailing code unit on some Windows builds.
  const size_t bufferSize = offsetof(FILE_RENAME_INFO, FileName) +
                            fileNameBytes + sizeof(wchar_t);
  if (destinationText.empty() || fileNameBytes > MAXDWORD ||
      bufferSize > MAXDWORD) {
    renameError = ERROR_INVALID_PARAMETER;
    return false;
  }

  std::vector<unsigned char> storage(bufferSize, 0);
  auto *renameInfo =
      reinterpret_cast<FILE_RENAME_INFO *>(storage.data());
  renameInfo->ReplaceIfExists = FALSE;
  renameInfo->RootDirectory = nullptr;
  renameInfo->FileNameLength = static_cast<DWORD>(fileNameBytes);
  std::memcpy(renameInfo->FileName, destinationText.data(), fileNameBytes);

  if (!SetFileInformationByHandle(
          fileHandle, FileRenameInfo, renameInfo,
          static_cast<DWORD>(storage.size()))) {
    renameError = GetLastError();
    return false;
  }

  renameError = ERROR_SUCCESS;
  return reportedIdentity == nullptr ||
         GetIdentity(fileHandle, *reportedIdentity);
}

fs::path MakeUniqueTestDirectory() {
  std::vector<wchar_t> tempPath(MAX_PATH + 1, L'\0');
  const DWORD pathLength =
      GetTempPathW(static_cast<DWORD>(tempPath.size()), tempPath.data());
  if (pathLength == 0 || pathLength >= tempPath.size())
    return {};

  for (unsigned int attempt = 0; attempt < 128; ++attempt) {
    const std::wstring component =
        L"ctSpaces-handle-rename-" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(GetTickCount64()) + L"-" +
        std::to_wstring(attempt);
    fs::path candidate = fs::path(tempPath.data()) / component;
    if (CreateDirectoryW(candidate.c_str(), nullptr))
      return candidate;
    if (GetLastError() != ERROR_ALREADY_EXISTS)
      return {};
  }
  return {};
}

class ScopedTestDirectory {
public:
  ScopedTestDirectory() : path_(MakeUniqueTestDirectory()) {}
  ~ScopedTestDirectory() {
    if (path_.empty())
      return;
    std::error_code ignored;
    fs::remove_all(path_, ignored);
  }

  const fs::path &Path() const noexcept { return path_; }

private:
  fs::path path_;
};

bool TestRenameTargetsOpenedObjectAfterPathReplacement(const fs::path &root) {
  const fs::path source = root / L"bound-source.bin";
  const fs::path parked = root / L"bound-original-parked.bin";
  const fs::path destination = root / L"bound-destination.bin";
  const std::string originalContents = "opened-original";
  const std::string replacementContents = "raced-replacement";

  if (!WriteNewFile(source, originalContents))
    return false;
  FileIdentity originalIdentity{};
  if (!GetPathIdentity(source, originalIdentity))
    return Require(false, L"Could not read the original source identity.");

  ScopedHandle opened = OpenForHandleRename(
      source, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
  if (!opened)
    return Require(false, L"Could not open the source for handle rename.");

  if (!MoveFileExW(source.c_str(), parked.c_str(), MOVEFILE_WRITE_THROUGH)) {
    ReportWindowsError(L"MoveFileExW(test race setup)", GetLastError());
    return false;
  }
  if (!WriteNewFile(source, replacementContents))
    return false;

  FileIdentity replacementIdentity{};
  if (!GetPathIdentity(source, replacementIdentity) ||
      !Require(replacementIdentity != originalIdentity,
               L"The raced replacement unexpectedly reused the live "
               L"original identity.")) {
    return false;
  }

  FileIdentity reportedIdentity{};
  DWORD renameError = ERROR_SUCCESS;
  if (!RenameOpenedFileNoReplace(opened.Get(), destination,
                                 &reportedIdentity, renameError)) {
    ReportWindowsError(L"SetFileInformationByHandle(handle-bound rename)",
                       renameError);
    return false;
  }
  if (!Require(reportedIdentity == originalIdentity,
               L"The successful rename reported the wrong opened-file "
               L"identity.")) {
    return false;
  }
  // Windows can keep the destination pathname unavailable until the handle
  // used for FileRenameInfo is closed. Identity is checked on that bound
  // handle first; pathname state is checked after closing it.
  opened.Reset();

  FileIdentity destinationIdentity{};
  FileIdentity currentSourceIdentity{};
  std::string destinationContents;
  std::string sourceContents;
  const bool readDestinationIdentity =
      GetPathIdentity(destination, destinationIdentity);
  if (!readDestinationIdentity) {
    ReportWindowsError(L"CreateFileW(renamed destination)", GetLastError());
    ReportDirectoryEntries(root);
    std::vector<wchar_t> finalPath(32768, L'\0');
    const DWORD finalPathLength = GetFinalPathNameByHandleW(
        opened.Get(), finalPath.data(), static_cast<DWORD>(finalPath.size()),
        FILE_NAME_NORMALIZED);
    if (finalPathLength != 0 && finalPathLength < finalPath.size())
      std::wcerr << L"Opened handle final path: " << finalPath.data() << L".\n";
  } else if (destinationIdentity != originalIdentity) {
    ReportIdentity(L"Original identity", originalIdentity);
    ReportIdentity(L"Destination identity", destinationIdentity);
    ReportIdentity(L"Handle-reported identity", reportedIdentity);
  }
  return Require(readDestinationIdentity &&
                     destinationIdentity == originalIdentity,
                 L"The destination does not contain the originally opened "
                 L"file.") &&
         Require(GetPathIdentity(source, currentSourceIdentity) &&
                     currentSourceIdentity == replacementIdentity,
                 L"The raced source replacement was moved instead of the "
                 L"opened file.") &&
         Require(PathIsMissing(parked),
                 L"The opened file remained at its parked race path.") &&
         Require(ReadFileContents(destination, destinationContents) &&
                     destinationContents == originalContents,
                 L"The handle-bound destination content is incorrect.") &&
         Require(ReadFileContents(source, sourceContents) &&
                     sourceContents == replacementContents,
                 L"The raced source replacement content was altered.");
}

bool TestProductionStyleLockRejectsSourceReplacement(const fs::path &root) {
  const fs::path source = root / L"locked-source.bin";
  const fs::path replacement = root / L"locked-race.bin";
  const fs::path destination = root / L"locked-destination.bin";
  if (!WriteNewFile(source, "locked-original") ||
      !WriteNewFile(replacement, "would-be-replacement")) {
    return false;
  }

  FileIdentity originalIdentity{};
  FileIdentity replacementIdentity{};
  if (!GetPathIdentity(source, originalIdentity) ||
      !GetPathIdentity(replacement, replacementIdentity)) {
    return Require(false, L"Could not capture pre-race file identities.");
  }

  ScopedHandle opened = OpenForHandleRename(source, FILE_SHARE_READ);
  if (!opened) {
    ReportWindowsError(L"CreateFileW(production-style rename lock)",
                       GetLastError());
    return false;
  }

  struct RaceResult {
    BOOL moved = FALSE;
    DWORD error = ERROR_SUCCESS;
  } race;
  std::thread racer([&]() {
    race.moved = MoveFileExW(replacement.c_str(), source.c_str(),
                             MOVEFILE_REPLACE_EXISTING |
                                 MOVEFILE_WRITE_THROUGH);
    race.error = race.moved ? ERROR_SUCCESS : GetLastError();
  });
  racer.join();

  FileIdentity sourceAfterRace{};
  FileIdentity replacementAfterRace{};
  if (!Require(!race.moved && race.error != ERROR_SUCCESS,
               L"A competing source replacement was not rejected by the "
               L"production-style lock.") ||
      !Require(GetPathIdentity(source, sourceAfterRace) &&
                   sourceAfterRace == originalIdentity,
               L"The source identity changed during the rejected race.") ||
      !Require(GetPathIdentity(replacement, replacementAfterRace) &&
                   replacementAfterRace == replacementIdentity,
               L"The rejected replacement file was moved or altered.")) {
    return false;
  }

  FileIdentity reportedIdentity{};
  DWORD renameError = ERROR_SUCCESS;
  if (!RenameOpenedFileNoReplace(opened.Get(), destination,
                                 &reportedIdentity, renameError)) {
    ReportWindowsError(L"SetFileInformationByHandle(locked rename)",
                       renameError);
    return false;
  }
  if (!Require(reportedIdentity == originalIdentity,
               L"The locked rename reported the wrong identity."))
    return false;
  opened.Reset();

  FileIdentity destinationIdentity{};
  return Require(GetPathIdentity(destination, destinationIdentity) &&
                     destinationIdentity == originalIdentity,
                 L"The locked rename committed the wrong file.") &&
         Require(PathIsMissing(source),
                 L"The locked source path still exists after rename.") &&
         Require(GetPathIdentity(replacement, replacementAfterRace) &&
                     replacementAfterRace == replacementIdentity,
                 L"The failed race's replacement file was not preserved.");
}

bool TestNoReplacePreservesExistingDestination(const fs::path &root) {
  const fs::path source = root / L"collision-source.bin";
  const fs::path destination = root / L"collision-destination.bin";
  const std::string sourceContents = "collision-source";
  const std::string destinationContents = "collision-destination";
  if (!WriteNewFile(source, sourceContents) ||
      !WriteNewFile(destination, destinationContents)) {
    return false;
  }

  FileIdentity sourceIdentity{};
  FileIdentity destinationIdentity{};
  if (!GetPathIdentity(source, sourceIdentity) ||
      !GetPathIdentity(destination, destinationIdentity)) {
    return Require(false, L"Could not capture collision identities.");
  }

  ScopedHandle opened = OpenForHandleRename(source, FILE_SHARE_READ);
  if (!opened)
    return Require(false, L"Could not lock the collision source.");

  FileIdentity reportedIdentity{};
  DWORD renameError = ERROR_SUCCESS;
  if (!Require(!RenameOpenedFileNoReplace(opened.Get(), destination,
                                          &reportedIdentity, renameError),
               L"A no-replace handle rename overwrote an existing "
               L"destination.") ||
      !Require(renameError != ERROR_SUCCESS,
               L"The rejected destination collision reported success.")) {
    return false;
  }

  FileIdentity sourceAfterFailure{};
  FileIdentity destinationAfterFailure{};
  FileIdentity handleAfterFailure{};
  std::string observedSourceContents;
  std::string observedDestinationContents;
  return Require(GetPathIdentity(source, sourceAfterFailure) &&
                     sourceAfterFailure == sourceIdentity,
                 L"A rejected collision changed the source identity.") &&
         Require(GetPathIdentity(destination, destinationAfterFailure) &&
                     destinationAfterFailure == destinationIdentity,
                 L"A rejected collision changed the destination identity.") &&
         Require(GetIdentity(opened.Get(), handleAfterFailure) &&
                     handleAfterFailure == sourceIdentity,
                 L"A rejected collision changed the opened handle identity.") &&
         Require(ReadFileContents(source, observedSourceContents) &&
                     observedSourceContents == sourceContents,
                 L"A rejected collision changed the source content.") &&
         Require(ReadFileContents(destination, observedDestinationContents) &&
                     observedDestinationContents == destinationContents,
                 L"A rejected collision changed the destination content.");
}

} // namespace

int wmain() {
  ScopedTestDirectory testDirectory;
  if (testDirectory.Path().empty()) {
    ReportWindowsError(L"CreateDirectoryW(test root)", GetLastError());
    return 1;
  }

  if (!TestRenameTargetsOpenedObjectAfterPathReplacement(
          testDirectory.Path()) ||
      !TestProductionStyleLockRejectsSourceReplacement(
          testDirectory.Path()) ||
      !TestNoReplacePreservesExistingDestination(testDirectory.Path())) {
    return 1;
  }

  std::wcout << L"Handle-bound no-replace rename tests passed.\n";
  return 0;
}
