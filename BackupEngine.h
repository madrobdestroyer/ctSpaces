#pragma once

#include "InProc7z.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

struct RestoreValidationSnapshot;
struct StagedRestore {
  std::filesystem::path stagingRoot;
  std::filesystem::path payloadDir;
  unsigned long long profileCount = 0;
  std::shared_ptr<const RestoreValidationSnapshot> validation;
};

// Seal the real staging roots and exact tree after successful archive/legacy
// validation. A reconstructed three-field StagedRestore is not commit-ready.
Result CaptureStagedRestoreIdentity(StagedRestore &stagedRestore,
                                    bool protectedExtraction = false);
#ifdef CTSPACES_BACKUP_TEST_HOOKS
// Fail one bound rename in native tests to verify the real rollback branch.
void FailRestoreRenameForTest(unsigned ordinal);
// Inject access-denied attempts inside the real bounded rename loop.
void DenyRestoreRenameAttemptsForTest(unsigned ordinal, unsigned failures,
                                     void (*beforeRetry)(void *) = nullptr,
                                     void *user = nullptr);
unsigned RestoreRenameAttemptsForTest();
enum class BackupPublicationBoundary { ManifestCreated, ArchiveCreated, Verified };
using BackupPublicationHook=void(*)(const std::filesystem::path& manifest,
    const std::filesystem::path& archive,BackupPublicationBoundary boundary,void* user);
void SetBackupPublicationHookForTest(BackupPublicationHook hook,void* user);
void FailBackupPublicationForTest();
#endif

using RestoreCommitGuard =
    bool (*)(void *user, std::wstring &failureDetails);

struct BackupOptions {
  // nullopt preserves the all-client backup contract. An explicit empty list
  // is invalid; selected names are copied and validated before enumeration.
  std::optional<std::vector<std::wstring>> selectedClientNames;
  _7zDetailedProgressCb detailedProgress = nullptr;
  void *detailedProgressUser = nullptr;
};

Result CreateValidated7zBackup(const std::filesystem::path &sitesDir,
                               const std::filesystem::path &destination,
                               const char *appVersion,
                               _7zProgressCb progressCb,
                               void *progressUser,
                               const BackupOptions &options = {});

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
