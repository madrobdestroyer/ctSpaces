#pragma once

#include <windows.h>

// In-process 7z pack/unpack wrappers based on the bundled 7-Zip SDK sources.
// - No password / encryption support.

enum class _7zOp : unsigned {
  Extract = 1,
  Compress = 2,
};

// percent is 0..100. currentItem may be nullptr.
using _7zProgressCb = void (*)(void *user, _7zOp op, unsigned percent,
                               const wchar_t *currentItem);

struct _7zArchiveInfo {
  unsigned long long itemCount = 0;
  unsigned long long fileCount = 0;
  unsigned long long directoryCount = 0;
  unsigned long long totalUncompressedBytes = 0;
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
                                 void *progressUser);
void _7zCloseRestoreSession(_7zRestoreSession *session);

// Read and CRC-test every item without writing extracted files.
HRESULT _7zTest7z(const wchar_t *archivePath, _7zProgressCb progressCb,
                  void *progressUser);

// Create a .7z archive from folderPath.
// If includeTopDirectory == false, the archive contains the folder's *contents* at the root
// (equivalent to: 7za a Archive.7z "folder\*" )
// If includeTopDirectory == true, the archive contains a top-level directory named after folderPath.
HRESULT _7zCompress7z(
    const wchar_t *archivePath, const wchar_t *folderPath,
    bool includeTopDirectory, _7zProgressCb progressCb, void *progressUser);

// Create an archive from a folder and one additional file. This lets ctSpaces
// add a root-level manifest without modifying or duplicating the Sites folder.
HRESULT _7zCompress7zWithExtraFile(
    const wchar_t *archivePath, const wchar_t *folderPath,
    bool includeTopDirectory, const wchar_t *extraFilePath,
    const wchar_t *extraArchivePath, _7zProgressCb progressCb,
    void *progressUser);
