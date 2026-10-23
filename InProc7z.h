#pragma once

#include <windows.h>
#include <string>
#include <vector>

// In-process 7z pack/unpack wrappers based on the bundled 7-Zip SDK sources.
// - No password / encryption support.

enum class _7zOp : unsigned {
  Extract = 1,
  Compress = 2,
};

// percent is 0..100. currentItem may be nullptr.
using _7zProgressCb = void (*)(void *user, _7zOp op, unsigned percent,
                               const wchar_t *currentItem);

enum class _7zProgressPhase {
  Preparing, Scanning, Compressing, CheckingSource, Verifying, Publishing
};

struct _7zProgressInfo {
  _7zProgressPhase phase = _7zProgressPhase::Preparing;
  unsigned long long completedBytes = 0;
  unsigned long long totalBytes = 0;
  unsigned long long processedItems = 0;
  unsigned long long totalItems = 0;
  bool totalKnown = false;
  // Callback-scoped relative path; copy it before retaining a report.
  const wchar_t *currentItem = nullptr;
};
// Reports may arrive synchronously from codec worker threads. Reports from
// one callback object are serialized; independent operations remain separate.
// Consumers must not wait on/reenter the same operation. A callback
// exception aborts work before publication; COM progress boundaries return E_ABORT.
using _7zDetailedProgressCb = void (*)(void *user, const _7zProgressInfo &info);

struct _7zArchiveInfo {
  unsigned long long itemCount = 0;
  unsigned long long fileCount = 0;
  unsigned long long directoryCount = 0;
  unsigned long long totalUncompressedBytes = 0;
  // Explicit direct directory entries beneath Sites in the inspected archive.
  // Backup publication compares this snapshot count with its manifest rather
  // than trusting a source-path count taken before compression binds the tree.
  unsigned long long sitesProfileCount = 0;
  unsigned long long sitesRootFileCount = 0;
  std::vector<std::wstring> sitesClientDirectoryNames;
};

// Opaque read-only archive session used to bind inspection and extraction to
// the same open file. While the session is alive, Windows sharing rules prevent
// the source archive from being replaced or opened for writing.
struct _7zRestoreSession;

namespace CtArchiveSafety {
inline constexpr unsigned long long kMaxItems = 500'000;
inline constexpr unsigned long long kMaxUncompressedBytes =
    256ull * 1024ull * 1024ull * 1024ull;
inline constexpr unsigned kMaxDirectoryDepth = 128;
} // namespace CtArchiveSafety

// Provide the application's HINSTANCE to 7-Zip runtime.
void _7zSetHInstance(HINSTANCE hInst);

// Extract a .7z archive to outDir.
HRESULT _7zExtra_7z(
    const wchar_t *archivePath, const wchar_t *outDir,
    _7zProgressCb progressCb, void *progressUser);

// Inspect metadata and reject paths that are unsafe to extract on Windows.
HRESULT _7zInspect7z(const wchar_t *archivePath, _7zArchiveInfo *info);

// Open and inspect an archive once, retaining the read-only file handle for a
// subsequent extraction. Close every successful session with
// _7zCloseRestoreSession.
HRESULT _7zOpenRestoreSession(const wchar_t *archivePath,
                              _7zArchiveInfo *info,
                              _7zRestoreSession **session);
HRESULT _7zExtractRestoreSession(_7zRestoreSession *session,
                                 const wchar_t *outDir,
                                 _7zProgressCb progressCb,
                                 void *progressUser,
                                 bool ownsEmptyOutputRoot = false,
                                 HANDLE protectedRoot = INVALID_HANDLE_VALUE,
                                 std::wstring *errorDetails = nullptr);
// Identity-only inventory of a completed validation seal. The relative root
// entry is empty. The session still holds its created objects while this is
// compared; unrelated additions must never become owned merely by being seen.
struct _7zRestoreObjectIdentity {
  std::wstring relativePath;
  DWORD volume=0,indexHigh=0,indexLow=0;
  bool directory=false;
};
HRESULT _7zVerifyRestoreOutputInventory(_7zRestoreSession *session,
    const std::vector<_7zRestoreObjectIdentity>& sealedObjects);
// Delete only original objects created by this extraction. Foreign additions,
// changed file identities and nonempty directories remain available for recovery.
bool _7zCleanupRestoreOutput(_7zRestoreSession *session);
#ifdef CTSPACES_BACKUP_TEST_HOOKS
void _7zDenyCleanupParentAttemptsForTest(unsigned attempts);
unsigned _7zCleanupParentAttemptsForTest();
void _7zFailNextOutputOwnershipAllocationForTest();
bool _7zOutputOwnershipAllocationFailureHitForTest();
#endif
void _7zCloseRestoreSession(_7zRestoreSession *session);

// Read and CRC-test every item without writing extracted files.
HRESULT _7zTest7z(const wchar_t *archivePath, _7zProgressCb progressCb,
                  void *progressUser);

// Inspect and CRC-test the caller's already-bound regular archive object.
// A duplicate handle is read; no pathname open or sharing-mode transition is
// required, so a DELETE-capable publication lease can remain held throughout.
// DuplicateHandle shares the file pointer: this call changes its position and
// the caller must not read/seek that object concurrently with inspection/test.
HRESULT _7zInspectAndTest7zHandle(HANDLE archive, _7zArchiveInfo *info,
                                  _7zProgressCb progressCb, void *progressUser,
                                  _7zDetailedProgressCb detailedCb = nullptr,
                                  void *detailedUser = nullptr);

// Create a .7z archive from folderPath.
// If includeTopDirectory == false, the archive contains the folder's *contents* at the root
// (equivalent to: 7za a Archive.7z "folder\*" )
// If includeTopDirectory == true, the archive contains a top-level directory named after folderPath.
HRESULT _7zCompress7z(
    const wchar_t *archivePath, const wchar_t *folderPath,
    bool includeTopDirectory, _7zProgressCb progressCb, void *progressUser);

// Create an archive from a folder and one additional file. This lets ctSpaces
// add a root-level manifest without modifying or duplicating the Sites folder.
// outputCreated is set only when this call atomically creates its output file.
// Callers can use it to clean up failed writes without deleting a collision.
// outputIdentity records the actual CREATE_NEW object before any compression
// callback; it allows callers to reject substitutions and clean only that file.
// When outputLease is supplied, the created stream denies writes/deletes and a
// READ|WRITE|DELETE duplicate is transferred before callbacks, even on later
// compression failure. The caller closes it. Its file pointer is shared with
// compression, so it must not be read/seeked until this call returns.
HRESULT _7zCompress7zWithExtraFile(
    const wchar_t *archivePath, const wchar_t *folderPath,
    bool includeTopDirectory, const wchar_t *extraFilePath,
    const wchar_t *extraArchivePath, _7zProgressCb progressCb,
    void *progressUser, bool *outputCreated = nullptr,
    BY_HANDLE_FILE_INFORMATION *outputIdentity = nullptr,
    HANDLE *outputLease = nullptr,
    _7zDetailedProgressCb detailedCb = nullptr, void *detailedUser = nullptr,
    const std::vector<std::wstring> *selectedRootChildren = nullptr);
