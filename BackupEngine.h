#pragma once

#include "InProc7z.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace CtBackup {

namespace Detail {

// Shared by backup creation, restore staging, and focused validation tests.
// This mirrors the launcher-visible Windows directory-name contract without
// requiring a ctSpaces marker inside legacy profiles.
[[nodiscard]] bool
IsValidClientDirectoryName(std::wstring_view clientName) noexcept;

// ctSpaces does not opt into the process-wide long-path manifest contract.
// These limits exclude the terminating NUL and deliberately leave the
// documented legacy CreateDirectoryW headroom.
inline constexpr size_t kLegacyMaximumFilePathCharacters = 259;
inline constexpr size_t kLegacyMaximumDirectoryPathCharacters = 247;

// From a client root, the current Default.7z starter's deepest managed file
// and directory targets are respectively:
//   \Browsers\chrome\Profile\<125-character archive entry>
//   \Browsers\chrome\Profile\<98-character archive directory>
// Release validation measures Default.7z against these reserved tails.
inline constexpr size_t kNewClientManagedFileTailCharacters = 150;
inline constexpr size_t kNewClientManagedDirectoryTailCharacters = 123;

// The 240-unit syntax contract remains available for existing/backup
// compatibility. Filesystem operations still fail closed when a historical
// path exceeds what Win32 can inspect. This stricter check is for new client
// roots, missing browser slots, rename targets, and restore targets.
[[nodiscard]] bool IsNewClientTargetPathWithinLegacyBudget(
    const std::filesystem::path &sitesDir,
    std::wstring_view clientName) noexcept;

// Maps every item below sourceRoot to destinationRoot without changing the
// filesystem and verifies that file and directory targets fit their distinct
// legacy Win32 budgets.
[[nodiscard]] bool IsTreeTargetWithinLegacyPathBudget(
    const std::filesystem::path &sourceRoot,
    const std::filesystem::path &destinationRoot,
    std::wstring *errorDetails = nullptr) noexcept;

} // namespace Detail

struct Result {
  HRESULT code = S_OK;
  std::wstring details;

  [[nodiscard]] bool ok() const noexcept { return SUCCEEDED(code); }
};

struct StagedRestore {
  std::filesystem::path stagingRoot;
  std::filesystem::path payloadDir;
  unsigned long long profileCount = 0;
};

using RestoreCommitGuard =
    bool (*)(void *user, std::wstring &failureDetails);

Result CreateValidated7zBackup(const std::filesystem::path &sitesDir,
                               const std::filesystem::path &destination,
                               const char *appVersion,
                               _7zProgressCb progressCb,
                               void *progressUser);

Result StageValidated7zRestore(const std::filesystem::path &archivePath,
                               const std::filesystem::path &stagingRoot,
                               _7zProgressCb progressCb,
                               void *progressUser,
                               StagedRestore &stagedRestore);

Result CommitStagedRestore(const StagedRestore &stagedRestore,
                           const std::filesystem::path &currentSitesDir,
                           const std::filesystem::path &recoveryDir,
                           RestoreCommitGuard precommitGuard = nullptr,
                           void *precommitGuardUser = nullptr);

} // namespace CtBackup
