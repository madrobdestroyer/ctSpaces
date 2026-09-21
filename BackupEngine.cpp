#include "BackupEngine.h"

#include <windows.h>

#include <algorithm>
#include <charconv>
#include <climits>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

namespace CtBackup {

namespace Detail {

namespace {

constexpr size_t kMaxClientNameLength = 240;

bool EqualsOrdinalIgnoreCase(std::wstring_view left,
                             std::wstring_view right) noexcept {
  if (left.size() > static_cast<size_t>(INT_MAX) ||
      right.size() > static_cast<size_t>(INT_MAX)) {
    return false;
  }
  return CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
                              right.data(), static_cast<int>(right.size()),
                              TRUE) == CSTR_EQUAL;
}

bool HasAsciiPrefixIgnoreCase(std::wstring_view value,
                              std::wstring_view prefix) noexcept {
  return value.size() >= prefix.size() &&
         EqualsOrdinalIgnoreCase(value.substr(0, prefix.size()), prefix);
}

bool IsReservedDeviceName(std::wstring_view name) noexcept {
  const size_t dot = name.find(L'.');
  const std::wstring_view base = name.substr(0, dot);
  if (EqualsOrdinalIgnoreCase(base, L"CON") ||
      EqualsOrdinalIgnoreCase(base, L"PRN") ||
      EqualsOrdinalIgnoreCase(base, L"AUX") ||
      EqualsOrdinalIgnoreCase(base, L"NUL") ||
      EqualsOrdinalIgnoreCase(base, L"CLOCK$") ||
      EqualsOrdinalIgnoreCase(base, L"CONIN$") ||
      EqualsOrdinalIgnoreCase(base, L"CONOUT$")) {
    return true;
  }

  if (base.size() != 4 ||
      (!HasAsciiPrefixIgnoreCase(base, L"COM") &&
       !HasAsciiPrefixIgnoreCase(base, L"LPT"))) {
    return false;
  }

  const wchar_t suffix = base[3];
  return (suffix >= L'1' && suffix <= L'9') || suffix == L'\x00B9' ||
         suffix == L'\x00B2' || suffix == L'\x00B3';
}

bool TryGetNormalizedAbsolutePath(const fs::path &path,
                                  fs::path &normalized) noexcept {
  try {
    std::error_code error;
    normalized = fs::absolute(path, error).lexically_normal();
    return !error && !normalized.empty();
  } catch (...) {
    normalized.clear();
    return false;
  }
}

bool IsTargetPathWithinLegacyBudget(const fs::path &path,
                                    bool directory) noexcept {
  fs::path normalized;
  if (!TryGetNormalizedAbsolutePath(path, normalized))
    return false;
  const size_t length = normalized.native().size();
  return length <=
         (directory ? kLegacyMaximumDirectoryPathCharacters
                    : kLegacyMaximumFilePathCharacters);
}

} // namespace

bool IsValidClientDirectoryName(std::wstring_view clientName) noexcept {
  if (clientName.empty() || clientName.size() > kMaxClientNameLength ||
      EqualsOrdinalIgnoreCase(clientName, L"Default") ||
      EqualsOrdinalIgnoreCase(clientName, L"Temp") ||
      IsReservedDeviceName(clientName)) {
    return false;
  }

  constexpr std::wstring_view invalidCharacters = L"\\/:*?\"<>|";
  constexpr std::wstring_view trimmedWhitespace = L" \t\n\r\f\v";
  if (trimmedWhitespace.find(clientName.front()) != std::wstring_view::npos ||
      trimmedWhitespace.find(clientName.back()) != std::wstring_view::npos ||
      clientName.back() == L'.') {
    return false;
  }

  for (const wchar_t character : clientName) {
    if (character < 32 ||
        invalidCharacters.find(character) != std::wstring_view::npos) {
      return false;
    }
  }
  return true;
}

bool IsNewClientTargetPathWithinLegacyBudget(
    const fs::path &sitesDir, std::wstring_view clientName) noexcept {
  try {
    if (!IsValidClientDirectoryName(clientName))
      return false;
    fs::path clientRoot =
        sitesDir / fs::path(std::wstring(clientName));
    fs::path normalized;
    if (!TryGetNormalizedAbsolutePath(clientRoot, normalized))
      return false;
    const size_t rootLength = normalized.native().size();
    return rootLength <=
               kLegacyMaximumFilePathCharacters -
                   kNewClientManagedFileTailCharacters &&
           rootLength <=
               kLegacyMaximumDirectoryPathCharacters -
                   kNewClientManagedDirectoryTailCharacters;
  } catch (...) {
    return false;
  }
}

bool IsTreeTargetWithinLegacyPathBudget(
    const fs::path &sourceRoot, const fs::path &destinationRoot,
    std::wstring *errorDetails) noexcept {
  const auto fail = [errorDetails](const std::wstring &message) {
    if (errorDetails)
      *errorDetails = message;
    return false;
  };

  try {
    const DWORD sourceRootAttributes = GetFileAttributesW(sourceRoot.c_str());
    if (sourceRootAttributes == INVALID_FILE_ATTRIBUTES ||
        (sourceRootAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (sourceRootAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
      return fail(L"The source tree is not a safe local directory.");
    }
    if (!IsTargetPathWithinLegacyBudget(destinationRoot, true)) {
      return fail(L"The destination client folder exceeds the legacy Windows "
                  L"directory-path budget.");
    }

    std::error_code iteratorError;
    fs::recursive_directory_iterator iterator(
        sourceRoot, fs::directory_options::none, iteratorError);
    const fs::recursive_directory_iterator end;
    if (iteratorError)
      return fail(L"The source tree could not be enumerated safely.");

    while (iterator != end) {
      const fs::path sourcePath = iterator->path();
      const fs::path relativePath = sourcePath.lexically_relative(sourceRoot);
      if (relativePath.empty() || relativePath.is_absolute() ||
          *relativePath.begin() == L"..") {
        return fail(L"The source tree produced an invalid relative path.");
      }

      const DWORD attributes = GetFileAttributesW(sourcePath.c_str());
      if (attributes == INVALID_FILE_ATTRIBUTES) {
        return fail(L"A source item could not be inspected while checking "
                    L"the destination path budget.");
      }
      const bool directory =
          (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
      const fs::path destinationPath = destinationRoot / relativePath;
      if (!IsTargetPathWithinLegacyBudget(destinationPath, directory)) {
        return fail(std::format(
            L"The restored item would exceed the legacy Windows {}-path "
            L"budget: {}",
            directory ? L"directory" : L"file",
            destinationPath.wstring()));
      }

      iterator.increment(iteratorError);
      if (iteratorError)
        return fail(L"The source tree changed or could not be enumerated "
                    L"safely.");
    }

    if (errorDetails)
      errorDetails->clear();
    return true;
  } catch (...) {
    return fail(L"The destination path budget could not be validated.");
  }
}

} // namespace Detail

namespace {

constexpr wchar_t kManifestName[] = L"ctSpacesBackup.manifest";
constexpr unsigned long long kRestoreMetadataBytesPerItem = 16ull * 1024ull;
constexpr unsigned long long kMinimumFreeSpaceReserve =
    5ull * 1024ull * 1024ull * 1024ull;
constexpr uintmax_t kMaxManifestBytes = 16ull * 1024ull;

struct RemoveOnExit {
  fs::path path;
  bool enabled = true;

  ~RemoveOnExit() {
    if (!enabled || path.empty())
      return;
    std::error_code error;
    fs::remove_all(path, error);
  }
};

struct RestoreSessionCloser {
  void operator()(_7zRestoreSession *session) const noexcept {
    _7zCloseRestoreSession(session);
  }
};

using RestoreSessionPtr =
    std::unique_ptr<_7zRestoreSession, RestoreSessionCloser>;

Result Failure(HRESULT code, std::wstring details) {
  return {FAILED(code) ? code : E_FAIL, std::move(details)};
}

std::wstring Widen(const std::string &text) {
  if (text.empty())
    return {};
  int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                  static_cast<int>(text.size()), nullptr, 0);
  UINT codePage = CP_UTF8;
  DWORD flags = MB_ERR_INVALID_CHARS;
  if (count <= 0) {
    codePage = CP_ACP;
    flags = 0;
    count = MultiByteToWideChar(codePage, flags, text.data(),
                                static_cast<int>(text.size()), nullptr, 0);
  }
  if (count <= 0)
    return L"Unknown filesystem error.";
  std::wstring result(static_cast<size_t>(count), L'\0');
  MultiByteToWideChar(codePage, flags, text.data(),
                      static_cast<int>(text.size()), result.data(), count);
  return result;
}

bool ComponentEquals(const fs::path &left, const fs::path &right) {
  return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

void RemoveTrailingSeparators(fs::path &path) {
  while (path.has_relative_path() && path.filename().empty())
    path = path.parent_path();
}

bool TryNormalizeAbsolute(const fs::path &path, fs::path &normalized) {
  if (path.empty())
    return false;
  std::error_code error;
  normalized = fs::absolute(path, error).lexically_normal();
  RemoveTrailingSeparators(normalized);
  return !error && !normalized.empty();
}

bool TryNormalizeResolved(const fs::path &path, fs::path &normalized) {
  fs::path absolutePath;
  if (!TryNormalizeAbsolute(path, absolutePath))
    return false;
  std::error_code error;
  normalized = fs::weakly_canonical(absolutePath, error).lexically_normal();
  RemoveTrailingSeparators(normalized);
  return !error && !normalized.empty();
}

bool NormalizedPathIsWithin(const fs::path &normalizedCandidate,
                            const fs::path &normalizedRoot) {
  auto candidatePart = normalizedCandidate.begin();
  for (auto rootPart = normalizedRoot.begin(); rootPart != normalizedRoot.end();
       ++rootPart, ++candidatePart) {
    if (candidatePart == normalizedCandidate.end() ||
        !ComponentEquals(*candidatePart, *rootPart))
      return false;
  }
  return true;
}

bool TryIsPathWithin(const fs::path &candidate, const fs::path &root,
                     bool &isWithin) {
  isWithin = false;
  fs::path absoluteCandidate;
  fs::path absoluteRoot;
  fs::path resolvedCandidate;
  fs::path resolvedRoot;
  if (!TryNormalizeAbsolute(candidate, absoluteCandidate) ||
      !TryNormalizeAbsolute(root, absoluteRoot) ||
      !TryNormalizeResolved(candidate, resolvedCandidate) ||
      !TryNormalizeResolved(root, resolvedRoot)) {
    return false;
  }

  // Check both forms. The lexical check rejects a path located inside the
  // managed tree even when a reparse point leads out; the resolved check
  // rejects an outside-looking alias that leads back into the managed tree.
  isWithin = NormalizedPathIsWithin(absoluteCandidate, absoluteRoot) ||
             NormalizedPathIsWithin(resolvedCandidate, resolvedRoot);
  return true;
}

bool IsRealDirectory(const fs::path &path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
         (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

bool IsRealRegularFile(const fs::path &path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
         (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

Result CountProfiles(const fs::path &sitesDir, unsigned long long &count) {
  count = 0;
  if (!IsRealDirectory(sitesDir)) {
    return Failure(E_INVALIDARG,
                   L"The Sites folder is not a real local directory.");
  }

  for (const fs::directory_entry &entry : fs::directory_iterator(sitesDir)) {
    const std::wstring clientName = entry.path().filename().wstring();
    if (!Detail::IsValidClientDirectoryName(clientName)) {
      return Failure(E_INVALIDARG,
                     L"The Sites folder contains an invalid client directory "
                     L"name: " +
                         clientName);
    }
    if (!IsRealDirectory(entry.path())) {
      return Failure(E_INVALIDARG,
                     L"Every direct item in Sites must be a real client "
                     L"directory. Unexpected item: " +
                         clientName);
    }
    if (count == ULLONG_MAX)
      return Failure(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW),
                     L"The number of client profiles is too large.");
    ++count;
  }
  return {};
}

std::string UtcTimestamp() {
  SYSTEMTIME utc{};
  GetSystemTime(&utc);
  return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", utc.wYear,
                     utc.wMonth, utc.wDay, utc.wHour, utc.wMinute,
                     utc.wSecond);
}

Result WriteManifest(const fs::path &manifestPath, const char *appVersion,
                     unsigned long long profileCount) {
  if (!appVersion || !*appVersion)
    return Failure(E_INVALIDARG, L"The application version is missing.");

  std::ofstream output(manifestPath, std::ios::binary | std::ios::trunc);
  if (!output)
    return Failure(HRESULT_FROM_WIN32(ERROR_OPEN_FAILED),
                   L"The backup manifest could not be created.");

  output << "format=ctSpaces-backup\n"
         << "formatVersion=1\n"
         << "appVersion=" << appVersion << "\n"
         << "createdUtc=" << UtcTimestamp() << "\n"
         << "profileCount=" << profileCount << "\n"
         << "payload=Sites\n";
  output.flush();
  if (!output.good())
    return Failure(HRESULT_FROM_WIN32(ERROR_WRITE_FAULT),
                   L"The backup manifest could not be written.");
  return {};
}

Result ReadManifest(const fs::path &manifestPath,
                    unsigned long long &profileCount) {
  if (!fs::is_regular_file(manifestPath))
    return Failure(E_INVALIDARG,
                   L"This is not a validated ctSpaces backup: the manifest "
                   L"is missing.");
  if (fs::file_size(manifestPath) == 0 ||
      fs::file_size(manifestPath) > kMaxManifestBytes)
    return Failure(E_INVALIDARG,
                   L"This is not a valid ctSpaces backup manifest.");

  std::ifstream input(manifestPath, std::ios::binary);
  if (!input)
    return Failure(HRESULT_FROM_WIN32(ERROR_OPEN_FAILED),
                   L"The backup manifest could not be read.");

  std::map<std::string, std::string> fields;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    const size_t separator = line.find('=');
    if (separator == std::string::npos || separator == 0 ||
        !fields.emplace(line.substr(0, separator), line.substr(separator + 1))
             .second)
      return Failure(E_INVALIDARG,
                     L"The ctSpaces backup manifest is malformed.");
  }

  if (fields["format"] != "ctSpaces-backup" ||
      fields["formatVersion"] != "1" || fields["payload"] != "Sites" ||
      fields["appVersion"].empty() || fields["createdUtc"].empty() ||
      fields["profileCount"].empty())
    return Failure(E_INVALIDARG,
                   L"The backup manifest is missing required ctSpaces data.");

  const std::string &countText = fields["profileCount"];
  const char *begin = countText.data();
  const char *end = begin + countText.size();
  auto parsed = std::from_chars(begin, end, profileCount);
  if (parsed.ec != std::errc{} || parsed.ptr != end || profileCount == 0)
    return Failure(E_INVALIDARG,
                   L"The backup manifest has an invalid profile count.");
  return {};
}

fs::path TemporarySibling(const fs::path &path, const wchar_t *purpose) {
  return path.parent_path() /
         std::format(L"{}.{}-{}-{}", path.filename().wstring(), purpose,
                     GetCurrentProcessId(), GetTickCount64());
}

Result CheckArchiveLimits(const _7zArchiveInfo &info,
                          const fs::path &destinationRoot) {
  if (info.itemCount == 0 ||
      info.itemCount > CtArchiveSafety::kMaxItems)
    return Failure(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE),
                   L"The backup contains an unreasonable number of items.");
  if (info.totalUncompressedBytes >
      CtArchiveSafety::kMaxUncompressedBytes)
    return Failure(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE),
                   L"The expanded backup is too large to restore safely.");
  if (info.itemCount >
      (ULLONG_MAX - info.totalUncompressedBytes) /
          kRestoreMetadataBytesPerItem)
    return Failure(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW),
                   L"The backup size could not be validated safely.");
  const unsigned long long requiredBytes =
      info.totalUncompressedBytes +
      info.itemCount * kRestoreMetadataBytesPerItem;

  ULARGE_INTEGER available{};
  ULARGE_INTEGER total{};
  if (!GetDiskFreeSpaceExW(destinationRoot.c_str(), &available, nullptr,
                           &total))
    return Failure(HRESULT_FROM_WIN32(GetLastError()),
                   L"Available disk space could not be checked.");
  const unsigned long long freeSpaceReserve =
      (std::max)(kMinimumFreeSpaceReserve, total.QuadPart / 20);
  if (available.QuadPart <= freeSpaceReserve ||
      requiredBytes > available.QuadPart - freeSpaceReserve)
    return Failure(HRESULT_FROM_WIN32(ERROR_DISK_FULL),
                   L"There is not enough free disk space to stage this "
                   L"restore safely.");
  return {};
}

Result ValidateStagingRoot(const fs::path &stagingRoot,
                           StagedRestore &stagedRestore) {
  if (!IsRealDirectory(stagingRoot))
    return Failure(E_INVALIDARG,
                   L"The staged backup root is not a safe local directory.");

  bool foundManifest = false;
  bool foundSites = false;
  for (const fs::directory_entry &entry :
       fs::directory_iterator(stagingRoot)) {
    const std::wstring name = entry.path().filename().wstring();
    if (_wcsicmp(name.c_str(), kManifestName) == 0 &&
        IsRealRegularFile(entry.path())) {
      foundManifest = true;
    } else if (_wcsicmp(name.c_str(), L"Sites") == 0 &&
               IsRealDirectory(entry.path())) {
      foundSites = true;
    } else {
      return Failure(E_INVALIDARG,
                     L"The backup contains unexpected data at its root.");
    }
  }
  if (!foundManifest || !foundSites)
    return Failure(E_INVALIDARG,
                   L"The backup does not contain the expected ctSpaces "
                   L"manifest and Sites folder.");

  unsigned long long declaredProfiles = 0;
  Result manifestResult =
      ReadManifest(stagingRoot / kManifestName, declaredProfiles);
  if (!manifestResult.ok())
    return manifestResult;

  const fs::path payloadDir = stagingRoot / L"Sites";
  unsigned long long actualProfiles = 0;
  Result countResult = CountProfiles(payloadDir, actualProfiles);
  if (!countResult.ok())
    return countResult;
  if (actualProfiles == 0 || actualProfiles != declaredProfiles)
    return Failure(E_INVALIDARG,
                   L"The backup profile count does not match its manifest.");

  stagedRestore.stagingRoot = stagingRoot;
  stagedRestore.payloadDir = payloadDir;
  stagedRestore.profileCount = actualProfiles;
  return {};
}

} // namespace

Result CreateValidated7zBackup(const fs::path &sitesDir,
                               const fs::path &destination,
                               const char *appVersion,
                               _7zProgressCb progressCb,
                               void *progressUser) {
  try {
    if (!IsRealDirectory(sitesDir))
      return Failure(HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND),
                     L"The Sites folder does not exist as a real local "
                     L"directory.");
    if (_wcsicmp(destination.extension().c_str(), L".7z") != 0)
      return Failure(E_INVALIDARG,
                     L"New ctSpaces backups must use the .7z extension.");

    fs::path absoluteSites;
    if (!TryNormalizeAbsolute(sitesDir, absoluteSites) ||
        absoluteSites.parent_path().empty()) {
      return Failure(E_INVALIDARG,
                     L"The managed ctSpaces data folder could not be "
                     L"validated safely.");
    }
    const fs::path managedDataRoot = absoluteSites.parent_path();
    bool destinationIsManaged = false;
    if (!TryIsPathWithin(destination, managedDataRoot,
                         destinationIsManaged)) {
      return Failure(E_INVALIDARG,
                     L"The selected backup destination could not be "
                     L"validated safely.");
    }
    if (destinationIsManaged)
      return Failure(E_INVALIDARG,
                     L"A backup cannot replace or be saved inside the "
                     L"ctSpaces data folder.");

    unsigned long long profileCount = 0;
    Result countResult = CountProfiles(sitesDir, profileCount);
    if (!countResult.ok())
      return countResult;
    if (profileCount == 0)
      return Failure(HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
                     L"No client profiles were found to back up.");

    const fs::path manifestPath =
        TemporarySibling(sitesDir.parent_path() / kManifestName, L"writing");
    const fs::path stagedArchive = TemporarySibling(destination, L"partial");
    RemoveOnExit manifestCleanup{manifestPath};
    RemoveOnExit archiveCleanup{stagedArchive};
    fs::remove(stagedArchive);

    Result manifestResult =
        WriteManifest(manifestPath, appVersion, profileCount);
    if (!manifestResult.ok())
      return manifestResult;

    HRESULT hr = _7zCompress7zWithExtraFile(
        stagedArchive.c_str(), sitesDir.c_str(), true, manifestPath.c_str(),
        kManifestName, progressCb, progressUser);
    if (FAILED(hr))
      return Failure(hr, L"7-Zip could not create a complete backup.");

    _7zArchiveInfo info{};
    hr = _7zInspect7z(stagedArchive.c_str(), &info);
    if (FAILED(hr) || info.itemCount < profileCount + 2)
      return Failure(FAILED(hr) ? hr : E_FAIL,
                     L"The finished backup did not contain all expected "
                     L"entries.");

    hr = _7zTest7z(stagedArchive.c_str(), progressCb, progressUser);
    if (FAILED(hr))
      return Failure(hr,
                     L"The finished backup failed its 7-Zip integrity test.");
    if (!fs::is_regular_file(stagedArchive) ||
        fs::file_size(stagedArchive) == 0)
      return Failure(E_FAIL, L"The finished backup file is empty.");

    if (!MoveFileExW(stagedArchive.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      return Failure(HRESULT_FROM_WIN32(GetLastError()),
                     L"The verified backup could not replace the selected "
                     L"destination file.");
    return {};
  } catch (const std::exception &error) {
    return Failure(E_FAIL, Widen(error.what()));
  }
}

Result StageValidated7zRestore(const fs::path &archivePath,
                               const fs::path &stagingRoot,
                               _7zProgressCb progressCb,
                               void *progressUser,
                               StagedRestore &stagedRestore) {
  stagedRestore = {};
  try {
    if (!fs::is_regular_file(archivePath))
      return Failure(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),
                     L"The selected backup file does not exist.");

    fs::remove_all(stagingRoot);
    fs::create_directories(stagingRoot);
    RemoveOnExit stagingCleanup{stagingRoot};

    _7zArchiveInfo info{};
    _7zRestoreSession *openedSession = nullptr;
    HRESULT hr =
        _7zOpenRestoreSession(archivePath.c_str(), &info, &openedSession);
    RestoreSessionPtr session(openedSession);
    if (FAILED(hr))
      return Failure(hr,
                     L"The backup is damaged or contains an unsafe path.");
    Result limitResult = CheckArchiveLimits(info, stagingRoot);
    if (!limitResult.ok())
      return limitResult;

    hr = _7zExtractRestoreSession(session.get(), stagingRoot.c_str(),
                                  progressCb, progressUser);
    if (FAILED(hr))
      return Failure(hr,
                     L"The backup failed its integrity check while being "
                     L"extracted.");
    session.reset();

    Result validationResult = ValidateStagingRoot(stagingRoot, stagedRestore);
    if (!validationResult.ok())
      return validationResult;
    stagingCleanup.enabled = false;
    return {};
  } catch (const std::exception &error) {
    return Failure(E_FAIL, Widen(error.what()));
  }
}

Result CommitStagedRestore(const StagedRestore &stagedRestore,
                           const fs::path &currentSitesDir,
                           const fs::path &recoveryDir,
                           RestoreCommitGuard precommitGuard,
                           void *precommitGuardUser) {
  bool previousMoved = false;
  try {
    if (!fs::is_directory(stagedRestore.stagingRoot) ||
        !fs::is_directory(stagedRestore.payloadDir) ||
        !fs::equivalent(stagedRestore.payloadDir,
                        stagedRestore.stagingRoot / L"Sites"))
      return Failure(E_INVALIDARG,
                     L"The staged restore payload is no longer valid.");
    if (fs::exists(recoveryDir))
      return Failure(HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS),
                     L"The pre-restore recovery folder already exists.");

    for (const fs::directory_entry &entry :
         fs::directory_iterator(stagedRestore.payloadDir)) {
      const std::wstring clientName = entry.path().filename().wstring();
      if (!IsRealDirectory(entry.path()) ||
          !Detail::IsValidClientDirectoryName(clientName)) {
        return Failure(
            E_INVALIDARG,
            L"The staged restore contains an invalid client directory.");
      }
      if (!Detail::IsNewClientTargetPathWithinLegacyBudget(
              currentSitesDir, clientName)) {
        return Failure(
            HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE),
            L"The client '" + clientName +
                L"' is too long for this ctSpaces data-folder location. "
                L"Current profile data was left unchanged.");
      }
      std::wstring pathBudgetError;
      if (!Detail::IsTreeTargetWithinLegacyPathBudget(
              entry.path(), currentSitesDir / clientName,
              &pathBudgetError)) {
        return Failure(
            HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE),
            L"The client '" + clientName +
                L"' contains data that would exceed the safe Windows path "
                L"budget at this ctSpaces data-folder location. " +
                pathBudgetError);
      }
    }

    const bool currentSitesExists = fs::exists(currentSitesDir);
    if (currentSitesExists) {
      std::wstring recoveryPathBudgetError;
      if (!Detail::IsTreeTargetWithinLegacyPathBudget(
              currentSitesDir, recoveryDir, &recoveryPathBudgetError)) {
        return Failure(
            HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE),
            L"The current Sites data would exceed the safe Windows path "
            L"budget in the pre-restore recovery folder. Current profile "
            L"data was left unchanged. " +
                recoveryPathBudgetError);
      }
    }
    if (precommitGuard) {
      std::wstring guardFailure;
      if (!precommitGuard(precommitGuardUser, guardFailure)) {
        if (guardFailure.empty())
          guardFailure =
              L"The final restore safety check rejected the commit.";
        return Failure(HRESULT_FROM_WIN32(ERROR_BUSY),
                       std::move(guardFailure));
      }
    }

    if (currentSitesExists) {
      fs::rename(currentSitesDir, recoveryDir);
      previousMoved = true;
    }
    fs::rename(stagedRestore.payloadDir, currentSitesDir);

    std::error_code cleanupError;
    fs::remove_all(stagedRestore.stagingRoot, cleanupError);
    Result result{};
    if (cleanupError)
      result.details =
          L"Restore completed, but its temporary folder could not be removed.";
    return result;
  } catch (const std::exception &error) {
    std::wstring details = Widen(error.what());
    if (previousMoved && !fs::exists(currentSitesDir) &&
        fs::exists(recoveryDir)) {
      try {
        fs::rename(recoveryDir, currentSitesDir);
      } catch (const std::exception &rollbackError) {
        details += L" Rollback also failed: " + Widen(rollbackError.what());
      }
    }
    return Failure(E_FAIL, std::move(details));
  }
}

} // namespace CtBackup
