#include "BackupEngine.h"
#include "SiblingStageName.h"
#include "ClientNameComparison.h"

#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
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

#ifdef CTSPACES_BACKUP_TEST_HOOKS
HRESULT CtBackupTestCreateGuid(GUID *identity);
#endif

namespace CtBackup {

struct RestoreValidationEntry {
  fs::path relative;
  BY_HANDLE_FILE_INFORMATION information{};
  std::array<unsigned char, 32> digest{};
};
struct RestoreValidationSnapshot {
  fs::path root;
  fs::path payload;
  unsigned long long profiles = 0;
  std::vector<RestoreValidationEntry> entries;
};

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

struct RestoreHandle {
  HANDLE value = INVALID_HANDLE_VALUE;
  RestoreHandle() = default;
  explicit RestoreHandle(HANDLE handle) : value(handle) {}
  ~RestoreHandle() { reset(); }
  RestoreHandle(const RestoreHandle &) = delete;
  RestoreHandle &operator=(const RestoreHandle &) = delete;
  void reset(HANDLE handle = INVALID_HANDLE_VALUE) {
    if (value != INVALID_HANDLE_VALUE && value != nullptr) CloseHandle(value);
    value = handle;
  }
  explicit operator bool() const { return value != INVALID_HANDLE_VALUE && value != nullptr; }
};

bool SameRestoreIdentity(const BY_HANDLE_FILE_INFORMATION &left,
                         const BY_HANDLE_FILE_INFORMATION &right) {
  return left.dwVolumeSerialNumber == right.dwVolumeSerialNumber &&
      left.nFileIndexHigh == right.nFileIndexHigh && left.nFileIndexLow == right.nFileIndexLow;
}

struct BackupArtifact {
  fs::path path;
  bool enabled=false;
  bool leaseCanDelete=false;
  BY_HANDLE_FILE_INFORMATION identity{};
  RestoreHandle lease;
  ~BackupArtifact(){Remove();}
  Result Bind(bool publication=false) {
    if(!lease){lease.reset(CreateFileW(path.c_str(),GENERIC_READ|(publication?(GENERIC_WRITE|DELETE|FILE_WRITE_ATTRIBUTES):0),
        FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));leaseCanDelete=publication;}
    if(!lease)return Failure(HRESULT_FROM_WIN32(GetLastError()),L"The temporary backup file could not be protected: "+path.wstring());
    BY_HANDLE_FILE_INFORMATION current{};
    if(!GetFileInformationByHandle(lease.value,&current) || !SameRestoreIdentity(identity,current) ||
       GetFileType(lease.value)!=FILE_TYPE_DISK || current.nNumberOfLinks!=1 ||
       (current.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))) {
      lease.reset();return Failure(HRESULT_FROM_WIN32(ERROR_FILE_INVALID),L"The temporary backup object changed; unrelated data was retained at "+path.wstring()+L".");
    }
    return {};
  }
  bool Remove() {
    if(!enabled || path.empty()){lease.reset();return true;}
    // SDK manifest readers require a read-only lease. On that transition,
    // reopen only for cleanup and accept only the originally created object.
    // The archive's creation/publication lease already has DELETE and stays
    // continuously held through its disposition or final publication.
    if(!leaseCanDelete)lease.reset();
    if(!lease)lease.reset(CreateFileW(path.c_str(),GENERIC_READ|DELETE|FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    leaseCanDelete=true;
    if(!lease) {
      const DWORD error=GetLastError();
      if(error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND){enabled=false;return true;}
      return false;
    }
    BY_HANDLE_FILE_INFORMATION current{};
    if(!GetFileInformationByHandle(lease.value,&current) || !SameRestoreIdentity(identity,current) ||
       current.nNumberOfLinks!=1 || (current.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return false;
    if(current.dwFileAttributes&FILE_ATTRIBUTE_READONLY) {
      FILE_BASIC_INFO basic{};
      if(!GetFileInformationByHandleEx(lease.value,FileBasicInfo,&basic,sizeof(basic)))return false;
      basic.FileAttributes&=~FILE_ATTRIBUTE_READONLY;
      if(!basic.FileAttributes)basic.FileAttributes=FILE_ATTRIBUTE_NORMAL;
      if(!SetFileInformationByHandle(lease.value,FileBasicInfo,&basic,sizeof(basic)))return false;
    }
    FILE_DISPOSITION_INFO disposition{TRUE};
    if(!SetFileInformationByHandle(lease.value,FileDispositionInfo,&disposition,sizeof(disposition)))return false;
    enabled=false;lease.reset();return true;
  }
};
bool SameRestoreMetadata(const BY_HANDLE_FILE_INFORMATION &left,
                         const BY_HANDLE_FILE_INFORMATION &right) {
  return SameRestoreIdentity(left, right) && left.dwFileAttributes == right.dwFileAttributes &&
      left.nFileSizeHigh == right.nFileSizeHigh && left.nFileSizeLow == right.nFileSizeLow &&
      CompareFileTime(&left.ftCreationTime, &right.ftCreationTime) == 0 &&
      CompareFileTime(&left.ftLastWriteTime, &right.ftLastWriteTime) == 0;
}
bool RestorePathMissing(const fs::path &path) {
  if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return false;
  const DWORD error = GetLastError();
  return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}
Result OpenRestoreDirectory(const fs::path &path, RestoreHandle &handle,
                             BY_HANDLE_FILE_INFORMATION &information, bool rename = false,
                             bool shareDelete = false, bool shareWrite = true) {
  handle.reset(CreateFileW(path.c_str(), GENERIC_READ | (rename ? DELETE : 0),
      FILE_SHARE_READ | (shareWrite ? FILE_SHARE_WRITE : 0) | (shareDelete ? FILE_SHARE_DELETE : 0), nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (!handle) return Failure(HRESULT_FROM_WIN32(GetLastError()), L"A restore directory could not be bound safely.");
  if (!GetFileInformationByHandle(handle.value, &information) ||
      !(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
      (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
    return Failure(E_INVALIDARG, L"A restore directory is a reparse point or changed object.");
  return {};
}

Result KeepRestoreParentNonempty(const fs::path& parent,RestoreHandle& sentinel) {
  const size_t available=parent.native().size()<Detail::kLegacyMaximumFilePathCharacters?
      Detail::kLegacyMaximumFilePathCharacters-parent.native().size()-1:0;
  if(!available)return Failure(HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE),L"The restore parent has no guard-file path budget.");
  for(unsigned attempt=0;attempt<64;++attempt) {
    GUID identity{};const HRESULT named=CoCreateGuid(&identity);
    if(FAILED(named))return Failure(named,L"A restore destination guard could not be named.");
    constexpr wchar_t digits[]=L"0123456789abcdef";std::wstring token;
    const auto* bytes=reinterpret_cast<const BYTE*>(&identity);
    for(size_t i=0;i<sizeof(identity);++i){token.push_back(digits[bytes[i]>>4]);token.push_back(digits[bytes[i]&15]);}
    const auto component=available==1?sibling_stage_name::BuildSingleCharacter(identity.Data1+attempt,false,available):
        sibling_stage_name::Build(token,false,available);
    if(component.empty())return Failure(HRESULT_FROM_WIN32(ERROR_INVALID_NAME),L"The restore destination guard has no valid filename.");
    const auto path=parent/component;
    sentinel.reset(CreateFileW(path.c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,nullptr,CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr));
    if(sentinel)return {};
    const DWORD error=GetLastError();
    if(error!=ERROR_FILE_EXISTS && error!=ERROR_ALREADY_EXISTS)
      return Failure(HRESULT_FROM_WIN32(error),L"The restore destination guard could not be reserved.");
  }
  return Failure(HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS),L"No vacant restore destination guard filename was available.");
}

Result AllowRestoreParentWrites(const fs::path& path,RestoreHandle& original) {
  BY_HANDLE_FILE_INFORMATION before{},after{};
  if(!GetFileInformationByHandle(original.value,&before))return Failure(HRESULT_FROM_WIN32(GetLastError()),L"The restore parent identity is unavailable.");
  RestoreHandle compatible;
  Result opened=OpenRestoreDirectory(path,compatible,after);
  if(!opened.ok())return opened;
  if(!SameRestoreIdentity(before,after))return Failure(HRESULT_FROM_WIN32(ERROR_FILE_INVALID),L"The protected restore parent changed.");
  original.reset(compatible.value);compatible.value=INVALID_HANDLE_VALUE;return {};
}

Result PinBackupOutputAncestors(const fs::path& parent,
    std::vector<std::unique_ptr<RestoreHandle>>& bindings) {
  fs::path current=parent;
  for(;;) {
    auto handle=std::make_unique<RestoreHandle>();BY_HANDLE_FILE_INFORMATION info{};
    const Result opened=OpenRestoreDirectory(current,*handle,info,false,false,false);
    if(!opened.ok())return opened;
    bindings.push_back(std::move(handle));
    const auto next=current.parent_path();if(next.empty() || next==current)break;current=next;
  }
  return {};
}

#ifdef CTSPACES_BACKUP_TEST_HOOKS
thread_local BackupPublicationHook backupPublicationHook=nullptr;
thread_local void* backupPublicationUser=nullptr;
thread_local bool failBackupPublication=false;
#endif
Result PublishBoundBackupFile(HANDLE file,const fs::path& destination) {
  BY_HANDLE_FILE_INFORMATION source{};
  if(!GetFileInformationByHandle(file,&source) || source.nNumberOfLinks!=1 ||
     (source.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))
    return Failure(HRESULT_FROM_WIN32(ERROR_FILE_INVALID),L"The protected backup file became unsafe before publication.");
  if(!FlushFileBuffers(file))return Failure(HRESULT_FROM_WIN32(GetLastError()),L"The verified backup could not be flushed before publication.");
  const auto name=fs::absolute(destination).lexically_normal().wstring();
  if(name.empty() || name.size()>(MAXDWORD-sizeof(FILE_RENAME_INFO))/sizeof(wchar_t))return Failure(E_INVALIDARG,L"The backup publication path is invalid.");
  std::vector<BYTE> data(offsetof(FILE_RENAME_INFO,FileName)+(name.size()+1)*sizeof(wchar_t),0);
  auto* rename=reinterpret_cast<FILE_RENAME_INFO*>(data.data());rename->ReplaceIfExists=TRUE;
  rename->FileNameLength=static_cast<DWORD>(name.size()*sizeof(wchar_t));memcpy(rename->FileName,name.data(),rename->FileNameLength);
#ifdef CTSPACES_BACKUP_TEST_HOOKS
  if(failBackupPublication){failBackupPublication=false;return Failure(HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION),L"Focused bound backup publication failure.");}
#endif
  if(!SetFileInformationByHandle(file,FileRenameInfo,rename,static_cast<DWORD>(data.size())))
    return Failure(HRESULT_FROM_WIN32(GetLastError()),L"The verified backup could not replace the selected destination file.");
  return {};
}

Result HashRestoreFile(HANDLE file, std::array<unsigned char, 32> &digest) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  struct CloseHash {
    BCRYPT_ALG_HANDLE &algorithm; BCRYPT_HASH_HANDLE &hash;
    ~CloseHash() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
  } cleanup{algorithm, hash};
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
    return Failure(E_FAIL, L"Restore content verification could not initialize SHA-256.");
  DWORD objectLength = 0, returned = 0;
  if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength),
                        sizeof(objectLength), &returned, 0) < 0)
    return Failure(E_FAIL, L"Restore content verification could not prepare SHA-256.");
  std::vector<unsigned char> object(objectLength);
  if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) < 0)
    return Failure(E_FAIL, L"Restore content verification could not create SHA-256.");
  std::array<unsigned char, 64 * 1024> buffer{};
  for (;;) {
    DWORD read = 0;
    if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
      return Failure(HRESULT_FROM_WIN32(GetLastError()), L"A staged restore file changed or could not be read.");
    if (read == 0) break;
    if (BCryptHashData(hash, buffer.data(), read, 0) < 0)
      return Failure(E_FAIL, L"Restore content verification failed.");
  }
  if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
    return Failure(E_FAIL, L"Restore content verification could not finish SHA-256.");
  return {};
}

Result CaptureRestoreNode(const fs::path &path, const fs::path &relative,
                           std::vector<RestoreValidationEntry> &entries, unsigned depth = 0,
                           bool boundRoots = false, bool protectedExtraction = false) {
  if (depth > CtArchiveSafety::kMaxDirectoryDepth || entries.size() >= CtArchiveSafety::kMaxItems)
    return Failure(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE), L"The staged restore tree exceeds its validation limits.");
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
    return Failure(E_INVALIDARG, L"The staged restore contains an uninspectable item or reparse point.");
  const bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  RestoreHandle file(CreateFileW(path.c_str(), GENERIC_READ,
      directory ? FILE_SHARE_READ | FILE_SHARE_WRITE | (protectedExtraction || (boundRoots && depth <= 1) ? FILE_SHARE_DELETE : 0) : FILE_SHARE_READ | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr));
  if (!file) return Failure(HRESULT_FROM_WIN32(GetLastError()), L"A staged restore item could not be bound.");
  RestoreValidationEntry entry; entry.relative = relative;
  if (!GetFileInformationByHandle(file.value, &entry.information) ||
      (entry.information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
      ((entry.information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory ||
      GetFileType(file.value) != FILE_TYPE_DISK || (!directory && entry.information.nNumberOfLinks != 1))
    return Failure(E_INVALIDARG, L"A staged restore item is a replaced object, reparse point or hard link.");
  if (!directory) {
    const Result hashed = HashRestoreFile(file.value, entry.digest);
    if (!hashed.ok()) return hashed;
  }
  entries.push_back(entry);
  if (directory) {
    for (const auto &child : fs::directory_iterator(path)) {
      const fs::path name = child.path().filename();
      if (name.empty() || name == L"." || name == L"..")
        return Failure(E_INVALIDARG, L"The staged restore produced an invalid child name.");
      const Result captured = CaptureRestoreNode(child.path(), relative / name, entries, depth + 1, boundRoots, protectedExtraction);
      if (!captured.ok()) return captured;
    }
  }
  BY_HANDLE_FILE_INFORMATION after{};
  if (!GetFileInformationByHandle(file.value, &after) || !SameRestoreMetadata(entry.information, after))
    return Failure(HRESULT_FROM_WIN32(ERROR_FILE_INVALID), L"The staged restore changed while being validated.");
  return {};
}

Result CaptureRestoreTree(const fs::path &root, std::vector<RestoreValidationEntry> &entries,
                           bool boundRoots = false, bool protectedExtraction = false) {
  entries.clear();
  const Result result = CaptureRestoreNode(root, {}, entries, 0, boundRoots, protectedExtraction);
  if (!result.ok()) return result;
  std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.relative.native() < b.relative.native(); });
  return {};
}
bool SameRestoreTree(const std::vector<RestoreValidationEntry> &left,
                     const std::vector<RestoreValidationEntry> &right) {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].relative != right[index].relative ||
        !SameRestoreMetadata(left[index].information, right[index].information) ||
        left[index].digest != right[index].digest) return false;
  }
  return true;
}
Result VerifyRestoreSnapshot(const StagedRestore &staged, bool boundRoots = false) {
  if (!staged.validation || staged.profileCount != staged.validation->profiles ||
      staged.stagingRoot != staged.validation->root || staged.payloadDir != staged.validation->payload ||
      !IsRealDirectory(staged.stagingRoot) || !IsRealDirectory(staged.payloadDir))
    return Failure(E_INVALIDARG, L"The staged restore is unsealed or its real roots changed after validation.");
  RestoreHandle root, payload;
  BY_HANDLE_FILE_INFORMATION rootInfo{}, payloadInfo{};
  Result opened = OpenRestoreDirectory(staged.stagingRoot, root, rootInfo, false, boundRoots);
  if (!opened.ok()) return opened;
  opened = OpenRestoreDirectory(staged.payloadDir, payload, payloadInfo, false, boundRoots);
  if (!opened.ok()) return opened;
  const auto payloadEntry = std::find_if(staged.validation->entries.begin(), staged.validation->entries.end(),
      [](const auto &entry) { return entry.relative.parent_path().empty() && ComponentEquals(entry.relative, L"Sites"); });
  if (staged.validation->entries.empty() || payloadEntry == staged.validation->entries.end() ||
      !SameRestoreIdentity(staged.validation->entries.front().information, rootInfo) ||
      !SameRestoreIdentity(payloadEntry->information, payloadInfo))
    return Failure(HRESULT_FROM_WIN32(ERROR_FILE_INVALID), L"The original staging root or payload directory was replaced.");
  std::vector<RestoreValidationEntry> current;
  const Result captured = CaptureRestoreTree(staged.stagingRoot, current, boundRoots);
  if (!captured.ok()) return captured;
  if (!SameRestoreTree(staged.validation->entries, current))
    return Failure(HRESULT_FROM_WIN32(ERROR_FILE_INVALID), L"The staged restore roots or contents changed after validation.");
  return {};
}

#ifdef CTSPACES_BACKUP_TEST_HOOKS
thread_local unsigned restoreRenameFailureOrdinal = 0;
thread_local unsigned restoreRenameDeniedOrdinal = 0, restoreRenameDeniedAttempts = 0;
thread_local unsigned restoreRenameObservedAttempts = 0;
thread_local void (*restoreRenameBeforeRetry)(void *) = nullptr;
thread_local void *restoreRenameRetryUser = nullptr;
#endif
Result RenameRestoreDirectory(HANDLE directory, const fs::path &destination) {
#ifdef CTSPACES_BACKUP_TEST_HOOKS
  if (restoreRenameFailureOrdinal != 0 && --restoreRenameFailureOrdinal == 0)
    return Failure(HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED), L"Focused bound restore rename failure.");
#endif
  const std::wstring name = fs::absolute(destination).lexically_normal().wstring();
  const size_t bytes = name.size() * sizeof(wchar_t);
  const size_t size = offsetof(FILE_RENAME_INFO, FileName) + bytes + sizeof(wchar_t);
  if (name.empty() || bytes > MAXDWORD || size > MAXDWORD)
    return Failure(E_INVALIDARG, L"The restore move destination is invalid.");
  std::vector<unsigned char> storage(size, 0);
  auto *rename = reinterpret_cast<FILE_RENAME_INFO *>(storage.data());
  rename->ReplaceIfExists = FALSE; rename->RootDirectory = nullptr;
  rename->FileNameLength = static_cast<DWORD>(bytes);
  std::memcpy(rename->FileName, name.data(), bytes);
  BY_HANDLE_FILE_INFORMATION original{};
  if (!GetFileInformationByHandle(directory, &original) ||
      !(original.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
      (original.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
    return Failure(HRESULT_FROM_WIN32(ERROR_FILE_INVALID),
                   L"The bound restore move source is unavailable or unsafe.");
#ifdef CTSPACES_BACKUP_TEST_HOOKS
  const bool inject = restoreRenameDeniedOrdinal != 0 && --restoreRenameDeniedOrdinal == 0;
  unsigned denied = inject ? restoreRenameDeniedAttempts : 0;
  auto beforeRetry = inject ? restoreRenameBeforeRetry : nullptr;
  void *retryUser = inject ? restoreRenameRetryUser : nullptr;
#endif
  constexpr unsigned maximumAttempts = 8;
  for (unsigned attempt = 0; attempt < maximumAttempts; ++attempt) {
    // A moved-but-open directory name can briefly reject its immediate reuse
    // with access denied. Keep every bound object/ancestor handle and retry
    // only that error, only while this exact source and vacant target remain.
    BY_HANDLE_FILE_INFORMATION current{};
    if (!GetFileInformationByHandle(directory, &current) ||
        !SameRestoreIdentity(original, current) ||
        !(current.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (current.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
      return Failure(HRESULT_FROM_WIN32(ERROR_FILE_INVALID),
                     L"The bound restore move source changed during retry.");
    if (!RestorePathMissing(destination))
      return Failure(HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS),
                     L"The restore move destination became occupied; existing data was not replaced.");
    BOOL moved = FALSE;
#ifdef CTSPACES_BACKUP_TEST_HOOKS
    if (inject) ++restoreRenameObservedAttempts;
    if (denied != 0) { --denied; SetLastError(ERROR_ACCESS_DENIED); }
    else
#endif
      moved = SetFileInformationByHandle(directory, FileRenameInfo, rename,
                                         static_cast<DWORD>(storage.size()));
    if (moved) return {};
    const DWORD moveError = GetLastError();
    if (moveError != ERROR_ACCESS_DENIED || attempt + 1 == maximumAttempts ||
        !RestorePathMissing(destination))
      return Failure(HRESULT_FROM_WIN32(moveError),
                     L"A verified restore directory could not be moved without replacing another item.");
#ifdef CTSPACES_BACKUP_TEST_HOOKS
    if (beforeRetry) { beforeRetry(retryUser); beforeRetry = nullptr; }
#endif
    Sleep(1u << attempt);
  }
  return Failure(E_FAIL, L"The verified restore directory move did not complete.");
}

Result VerifyInstalledRestore(const StagedRestore &staged, const fs::path &installed) {
  std::vector<RestoreValidationEntry> expected;
  for (const auto &original : staged.validation->entries) {
    auto part = original.relative.begin();
    if (part == original.relative.end() || !ComponentEquals(*part, L"Sites")) continue;
    RestoreValidationEntry entry = original;
    entry.relative.clear();
    for (++part; part != original.relative.end(); ++part) entry.relative /= *part;
    expected.push_back(std::move(entry));
  }
  std::sort(expected.begin(), expected.end(), [](const auto &a, const auto &b) { return a.relative.native() < b.relative.native(); });
  std::vector<RestoreValidationEntry> actual;
  const Result captured = CaptureRestoreTree(installed, actual, true);
  if (!captured.ok()) return captured;
  if (!SameRestoreTree(expected, actual))
    return Failure(HRESULT_FROM_WIN32(ERROR_FILE_INVALID), L"The installed restore changed during promotion.");
  return {};
}

bool CleanupBoundRestoreStage(const StagedRestore &staged, HANDLE stage) {
  // Delete only the original, sealed root files, through their own handles.
  // Never recursively follow a staging pathname after promotion.
  for (const auto &entry : staged.validation->entries) {
    if (entry.relative.empty() || !entry.relative.parent_path().empty() ||
        ComponentEquals(entry.relative, L"Sites")) continue;
    if (entry.information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
    RestoreHandle file(CreateFileW((staged.stagingRoot / entry.relative).c_str(), DELETE | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION information{};
    if (!file || !GetFileInformationByHandle(file.value, &information) ||
        !SameRestoreIdentity(entry.information, information) ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    FILE_DISPOSITION_INFO disposition{TRUE};
    if (!SetFileInformationByHandle(file.value, FileDispositionInfo, &disposition, sizeof(disposition))) return false;
  }
  FILE_DISPOSITION_INFO disposition{TRUE};
  return SetFileInformationByHandle(stage, FileDispositionInfo, &disposition, sizeof(disposition)) != FALSE;
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
                     unsigned long long profileCount, bool &fileCreated,
                     BY_HANDLE_FILE_INFORMATION &identity,std::string& expectedContents) {
  fileCreated = false;
  if (!appVersion || !*appVersion)
    return Failure(E_INVALIDARG, L"The application version is missing.");

  const HANDLE output = CreateFileW(manifestPath.c_str(), GENERIC_WRITE, 0,
                                     nullptr, CREATE_NEW,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
  if (output == INVALID_HANDLE_VALUE)
    return Failure(HRESULT_FROM_WIN32(GetLastError()),
                   L"The backup manifest could not be created.");
  fileCreated = true;
  struct CloseOutput {
    HANDLE handle;
    ~CloseOutput() { CloseHandle(handle); }
  } closer{output};
  if(!GetFileInformationByHandle(output,&identity))
    return Failure(HRESULT_FROM_WIN32(GetLastError()),L"The created backup manifest identity could not be retained.");
  expectedContents = std::format(
      "format=ctSpaces-backup\nformatVersion=1\nappVersion={}\n"
      "createdUtc={}\nprofileCount={}\npayload=Sites\n",
      appVersion, UtcTimestamp(), profileCount);
  const auto& contents=expectedContents;
  DWORD written = 0;
  if (contents.size() > MAXDWORD ||
      !WriteFile(output, contents.data(), static_cast<DWORD>(contents.size()),
                   &written, nullptr) || written != contents.size() ||
      !FlushFileBuffers(output))
    return Failure(HRESULT_FROM_WIN32(ERROR_WRITE_FAULT),
                   L"The backup manifest could not be written.");
  return {};
}

Result VerifyBackupManifestBytes(HANDLE manifest,const std::string& expected) {
  LARGE_INTEGER size{},begin{};
  if(expected.size()>MAXDWORD || !GetFileSizeEx(manifest,&size) ||
      size.QuadPart!=static_cast<LONGLONG>(expected.size()) ||
      !SetFilePointerEx(manifest,begin,nullptr,FILE_BEGIN))
    return Failure(HRESULT_FROM_WIN32(ERROR_INVALID_DATA),L"The temporary backup manifest no longer matches its created contents.");
  std::string current(expected.size(),'\0');size_t offset=0;
  while(offset<current.size()) {
    DWORD read=0;
    if(!ReadFile(manifest,current.data()+offset,static_cast<DWORD>(current.size()-offset),&read,nullptr) || !read)
      return Failure(HRESULT_FROM_WIN32(ERROR_READ_FAULT),L"The created backup manifest could not be verified safely.");
    offset+=read;
  }
  if(current!=expected)return Failure(HRESULT_FROM_WIN32(ERROR_INVALID_DATA),L"The temporary backup manifest no longer matches its created contents.");
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

Result ShortTemporarySibling(const fs::path &destination, fs::path &path) {
  // Eleven characters fit beside any directory in our legacy Win32 budget.
  // CREATE_NEW remains the authority: a colliding file is never overwritten.
  for (unsigned attempt = 0; attempt < 64; ++attempt) {
    GUID identity{};
#ifdef CTSPACES_BACKUP_TEST_HOOKS
    const HRESULT hr = CtBackupTestCreateGuid(&identity);
#else
    const HRESULT hr = CoCreateGuid(&identity);
#endif
    if (FAILED(hr))
      return Failure(hr, L"A unique backup staging name could not be created.");
    path = destination.parent_path() / std::format(L"{:08X}.7z", identity.Data1);
    // A manifest must never occupy the final filename, even if that selected
    // file does not exist yet. Its cleanup must not remove the finished backup.
    if (!ComponentEquals(path.filename(), destination.filename()))
      return {};
  }
  return Failure(HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS),
                 L"A separate backup staging name could not be created.");
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
                           StagedRestore &stagedRestore, bool protectedExtraction = false) {
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
  return CaptureStagedRestoreIdentity(stagedRestore,protectedExtraction);
}

} // namespace

#ifdef CTSPACES_BACKUP_TEST_HOOKS
void FailRestoreRenameForTest(unsigned ordinal) { restoreRenameFailureOrdinal = ordinal; }
void DenyRestoreRenameAttemptsForTest(unsigned ordinal, unsigned failures,
                                     void (*beforeRetry)(void *), void *user) {
  restoreRenameDeniedOrdinal = ordinal; restoreRenameDeniedAttempts = failures;
  restoreRenameObservedAttempts = 0; restoreRenameBeforeRetry = beforeRetry;
  restoreRenameRetryUser = user;
}
unsigned RestoreRenameAttemptsForTest() { return restoreRenameObservedAttempts; }
void SetBackupPublicationHookForTest(BackupPublicationHook hook,void* user) { backupPublicationHook=hook;backupPublicationUser=user;if(!hook)failBackupPublication=false; }
void FailBackupPublicationForTest() { failBackupPublication=true; }
#endif
Result CaptureStagedRestoreIdentity(StagedRestore &stagedRestore,bool protectedExtraction) {
  stagedRestore.validation.reset();
  try {
    fs::path root, payload;
    if (!TryNormalizeAbsolute(stagedRestore.stagingRoot, root) ||
        !TryNormalizeAbsolute(stagedRestore.payloadDir, payload) ||
        !IsRealDirectory(root) || !IsRealDirectory(payload) ||
        !fs::equivalent(payload, root / L"Sites"))
      return Failure(E_INVALIDARG, L"The restore roots are not real, matching local directories.");
    unsigned long long profiles = 0;
    Result counted = CountProfiles(payload, profiles);
    if (!counted.ok()) return counted;
    if (profiles == 0 || profiles != stagedRestore.profileCount)
      return Failure(E_INVALIDARG, L"The staged restore profile count changed before identity capture.");
    auto snapshot = std::make_shared<RestoreValidationSnapshot>();
    snapshot->root = root; snapshot->payload = payload; snapshot->profiles = profiles;
    const Result captured = CaptureRestoreTree(root, snapshot->entries,false,protectedExtraction);
    if (!captured.ok()) return captured;
    stagedRestore.stagingRoot = root; stagedRestore.payloadDir = payload;
    stagedRestore.validation = std::move(snapshot);
    return {};
  } catch (const std::exception &error) {
    return Failure(E_FAIL, Widen(error.what()));
  }
}

Result CreateValidated7zBackup(const fs::path &sitesDir,
                               const fs::path &destination,
                               const char *appVersion,
                               _7zProgressCb progressCb,
                               void *progressUser,
                               const BackupOptions &options) {
  try {
    // Own the exact user's choice and report destinations for this synchronous
    // operation. Later UI/client-list changes cannot broaden the backup.
    const BackupOptions operationOptions=options;
    const auto &selected=operationOptions.selectedClientNames;
    const auto report=[&](_7zProgressPhase phase)->Result {
      if(!operationOptions.detailedProgress)return {};
      try {
        const _7zProgressInfo info{phase};
        operationOptions.detailedProgress(operationOptions.detailedProgressUser,info);
        return {};
      } catch(...) {return Failure(E_ABORT,L"Backup progress reporting was interrupted before publication.");}
    };
    Result reported=report(_7zProgressPhase::Preparing);
    if(!reported.ok())return reported;
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

    fs::path absoluteDestination;
    if (!TryNormalizeAbsolute(destination, absoluteDestination) ||
        !Detail::IsTargetPathWithinLegacyBudget(absoluteDestination, false) ||
        !Detail::IsTargetPathWithinLegacyBudget(
            absoluteDestination.parent_path(), true))
      return Failure(HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE),
                     L"The selected backup path is too long for this "
                     L"Windows application. Choose a shorter folder path.");

    unsigned long long profileCount = 0;
    Result countResult;
    if(selected) {
      if(selected->empty())return Failure(E_INVALIDARG,L"Select at least one client to back up.");
      std::vector<std::wstring> names=*selected;
      std::sort(names.begin(),names.end(),[](const auto &left,const auto &right){return client_name_comparison::CompareIgnoringCase(left,right)<0;});
      for(size_t index=0;index<names.size();++index) {
        if(!Detail::IsValidClientDirectoryName(names[index]) ||
            (index && client_name_comparison::EqualIgnoringCase(names[index-1],names[index])))
          return Failure(E_INVALIDARG,L"The backup selection contains an invalid or duplicate client name.");
        if(!IsRealDirectory(sitesDir/names[index]))
          return Failure(HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND),L"A selected client is unavailable or is not a real directory: "+names[index]+L". No partial backup was created.");
      }
      profileCount=names.size();
    } else countResult=CountProfiles(sitesDir, profileCount);
    if (!countResult.ok())
      return countResult;
    if (profileCount == 0)
      return Failure(HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
                     L"No client profiles were found to back up.");

    std::vector<std::unique_ptr<RestoreHandle>> outputParents;
    Result boundParent=PinBackupOutputAncestors(absoluteDestination.parent_path(),outputParents);
    if(!boundParent.ok())return boundParent;
    BackupArtifact manifestCleanup,archiveCleanup;
    const auto finish=[&](Result result) {
      if(!archiveCleanup.Remove())result.details+=L" Temporary archive data was retained at "+archiveCleanup.path.wstring()+L".";
      if(!manifestCleanup.Remove())result.details+=L" Temporary manifest data was retained at "+manifestCleanup.path.wstring()+L".";
      return result;
    };
    Result manifestResult;
    std::string expectedManifest;
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
      manifestResult = ShortTemporarySibling(absoluteDestination,
                                             manifestCleanup.path);
      if (!manifestResult.ok())
        return finish(manifestResult);
      manifestResult = WriteManifest(manifestCleanup.path, appVersion,
                                     profileCount, manifestCleanup.enabled,manifestCleanup.identity,expectedManifest);
      if (manifestResult.ok() || manifestCleanup.enabled ||
          (manifestResult.code != HRESULT_FROM_WIN32(ERROR_FILE_EXISTS) &&
           manifestResult.code != HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)))
        break;
    }
    if (!manifestResult.ok())
      return finish(manifestResult);
#ifdef CTSPACES_BACKUP_TEST_HOOKS
    if(backupPublicationHook)backupPublicationHook(manifestCleanup.path,{},BackupPublicationBoundary::ManifestCreated,backupPublicationUser);
#endif
    manifestResult=manifestCleanup.Bind();
    if(!manifestResult.ok())return finish(manifestResult);
    manifestResult=VerifyBackupManifestBytes(manifestCleanup.lease.value,expectedManifest);
    if(!manifestResult.ok()) {
      // The strict read lease makes this comparison stable. Retain changed
      // same-object contents rather than deleting an independent writer's data.
      manifestCleanup.enabled=false;
      manifestResult.details+=L" Changed manifest data was retained at "+manifestCleanup.path.wstring()+L".";
      return finish(manifestResult);
    }
    // The original protected manifest keeps this parent nonempty. Allow native
    // rename metadata access only after binding its real identity and child.
    boundParent=AllowRestoreParentWrites(absoluteDestination.parent_path(),*outputParents.front());
    if(!boundParent.ok())return finish(boundParent);

    HRESULT hr = E_FAIL;
    archiveCleanup.leaseCanDelete=true;
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
      const Result stagingName = ShortTemporarySibling(
          absoluteDestination, archiveCleanup.path);
      if (!stagingName.ok())
        return finish(stagingName);
      hr = _7zCompress7zWithExtraFile(
          archiveCleanup.path.c_str(), sitesDir.c_str(), true,
          manifestCleanup.path.c_str(), kManifestName, progressCb,
          progressUser, &archiveCleanup.enabled,&archiveCleanup.identity,&archiveCleanup.lease.value,
          operationOptions.detailedProgress,operationOptions.detailedProgressUser,selected?&*selected:nullptr);
      if (SUCCEEDED(hr) || archiveCleanup.enabled ||
          (hr != HRESULT_FROM_WIN32(ERROR_FILE_EXISTS) &&
           hr != HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)))
        break;
    }
#ifdef CTSPACES_BACKUP_TEST_HOOKS
    if(archiveCleanup.enabled && backupPublicationHook)backupPublicationHook(manifestCleanup.path,archiveCleanup.path,BackupPublicationBoundary::ArchiveCreated,backupPublicationUser);
#endif
    if (FAILED(hr))
      return finish(Failure(hr, L"7-Zip could not create a complete backup."));
    const Result boundArchive=archiveCleanup.Bind(true);
    if(!boundArchive.ok())return finish(boundArchive);
    _7zArchiveInfo info{};
    hr = _7zInspectAndTest7zHandle(archiveCleanup.lease.value,&info,progressCb,progressUser,
        operationOptions.detailedProgress,operationOptions.detailedProgressUser);
    if (FAILED(hr))
      return finish(Failure(hr,
                     L"The finished backup failed its 7-Zip integrity test."));
    if(info.sitesRootFileCount)
      return finish(Failure(E_INVALIDARG,L"The backup snapshot contains unexpected files directly in Sites. Correct the client folder contents and retry."));
    for(const auto& name:info.sitesClientDirectoryNames)
      if(!Detail::IsValidClientDirectoryName(name))
        return finish(Failure(E_INVALIDARG,L"The backup snapshot contains an invalid client directory name: "+name+L". Correct the client folder contents and retry."));
    if(info.sitesProfileCount!=profileCount)
      return finish(Failure(HRESULT_FROM_WIN32(ERROR_FILE_INVALID),
          L"The client profile list changed while preparing the backup. Retry when the client list is stable."));
    if(selected) {
      auto expected=*selected,actual=info.sitesClientDirectoryNames;
      const auto less=[](const auto &left,const auto &right){return client_name_comparison::CompareIgnoringCase(left,right)<0;};
      std::sort(expected.begin(),expected.end(),less);std::sort(actual.begin(),actual.end(),less);
      if(expected.size()!=actual.size() || !std::equal(expected.begin(),expected.end(),actual.begin(),
          [](const auto &left,const auto &right){return client_name_comparison::EqualIgnoringCase(left,right);}))
        return finish(Failure(HRESULT_FROM_WIN32(ERROR_FILE_INVALID),L"The completed backup does not match the selected clients. The existing destination was preserved."));
    }
    if(info.itemCount<profileCount+2)return finish(Failure(E_FAIL,L"The finished backup did not contain all expected entries."));
    LARGE_INTEGER archiveBytes{};
    if(!GetFileSizeEx(archiveCleanup.lease.value,&archiveBytes) || archiveBytes.QuadPart<=0)
      return finish(Failure(E_FAIL,L"The finished backup file is empty or could not be inspected."));
#ifdef CTSPACES_BACKUP_TEST_HOOKS
    if(backupPublicationHook)backupPublicationHook(manifestCleanup.path,archiveCleanup.path,BackupPublicationBoundary::Verified,backupPublicationUser);
#endif
    reported=report(_7zProgressPhase::Publishing);
    if(!reported.ok())return finish(reported);
    const Result published=PublishBoundBackupFile(archiveCleanup.lease.value,absoluteDestination);
    if(!published.ok())return finish(published);
    archiveCleanup.enabled = false;
    return finish({});
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

    fs::create_directories(stagingRoot.parent_path());
    if (!fs::create_directory(stagingRoot) &&
        (!IsRealDirectory(stagingRoot) || !fs::is_empty(stagingRoot)))
      return Failure(HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS),
                     L"The restore staging location is occupied. Existing data was left unchanged.");
    RestoreHandle protectedRoot;
    BY_HANDLE_FILE_INFORMATION rootIdentity{};
    Result protectedResult=OpenRestoreDirectory(stagingRoot,protectedRoot,rootIdentity,true,false,false);
    if(!protectedResult.ok())return protectedResult;

    _7zArchiveInfo info{};
    _7zRestoreSession *openedSession = nullptr;
    HRESULT hr =
        _7zOpenRestoreSession(archivePath.c_str(), &info, &openedSession);
    RestoreSessionPtr session(openedSession);
    struct ExtractionCleanup {
      _7zRestoreSession* session;
      HANDLE root;
      bool enabled=true;
      ~ExtractionCleanup(){
        if(!enabled)return;
        if(session)_7zCleanupRestoreOutput(session);
        // Before extraction, and after its owned children have been removed,
        // dispose only the bound original root. Foreign additions keep it
        // nonempty and are retained; no path-based recursive deletion occurs.
        FILE_DISPOSITION_INFO disposition{TRUE};
        SetFileInformationByHandle(root,FileDispositionInfo,&disposition,sizeof(disposition));
      }
    } stagingCleanup{session.get(),protectedRoot.value};
    if (FAILED(hr))
      return Failure(hr,
                     L"The backup is damaged or contains an unsafe path.");
    Result limitResult = CheckArchiveLimits(info, stagingRoot);
    if (!limitResult.ok())
      return limitResult;

    hr = _7zExtractRestoreSession(session.get(), stagingRoot.c_str(),
                                  progressCb, progressUser,true,protectedRoot.value);
    if (FAILED(hr))
      return Failure(hr,
                     L"The backup failed its integrity check while being "
                     L"extracted.");
    Result validationResult = ValidateStagingRoot(stagingRoot, stagedRestore,true);
    if (!validationResult.ok()) {
      stagedRestore = {};
      return validationResult;
    }
    std::vector<_7zRestoreObjectIdentity> sealedObjects;
    sealedObjects.reserve(stagedRestore.validation->entries.size());
    for(const auto& entry:stagedRestore.validation->entries)
      sealedObjects.push_back({entry.relative.native(),entry.information.dwVolumeSerialNumber,
          entry.information.nFileIndexHigh,entry.information.nFileIndexLow,
          (entry.information.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0});
    hr=_7zVerifyRestoreOutputInventory(session.get(),sealedObjects);
    if(FAILED(hr)) {
      stagedRestore={};
      return Failure(hr,L"The extracted tree contains unexpected or replaced data. Staging data was retained at "+stagingRoot.wstring()+L".");
    }
    stagingCleanup.enabled = false;
    return {};
  } catch (const std::exception &error) {
    stagedRestore={};
    return Failure(E_FAIL, Widen(error.what()));
  }
}

Result CommitStagedRestore(const StagedRestore &stagedRestore,
                           const fs::path &currentSitesDir,
                           const fs::path &recoveryDir,
                           RestoreCommitGuard precommitGuard,
                           void *precommitGuardUser) {
  bool previousMoved = false;
  bool payloadMoved = false;
  RestoreHandle stage, payload, current, currentParent, recoveryParent,currentParentSentinel,recoveryParentSentinel;
  std::vector<std::unique_ptr<RestoreHandle>> ancestry;
  fs::path live, recovery;
  auto rejectWithRollback = [&](Result failure) {
    if (payloadMoved) {
      if (RestorePathMissing(stagedRestore.payloadDir)) {
        const Result restored = RenameRestoreDirectory(payload.value, stagedRestore.payloadDir);
        if (restored.ok()) payloadMoved = false;
        else failure.details += L" The staged payload could not be returned: " + restored.details;
      } else failure.details += L" The staging destination changed; the installed payload was retained for recovery.";
    }
    if (previousMoved) {
      if (RestorePathMissing(live)) {
        const Result restored = RenameRestoreDirectory(current.value, live);
        if (restored.ok()) previousMoved = false;
        else failure.details += L" The original Sites could not be returned: " + restored.details;
      } else failure.details += L" The live destination changed; the original Sites was retained in its recovery folder.";
    }
    return failure;
  };
  try {
    Result verified = VerifyRestoreSnapshot(stagedRestore);
    if (!verified.ok()) return verified;
    live = fs::absolute(currentSitesDir).lexically_normal();
    recovery = fs::absolute(recoveryDir).lexically_normal();
    for (const auto &pair : {std::pair{live, stagedRestore.stagingRoot},
                            std::pair{recovery, stagedRestore.stagingRoot},
                            std::pair{live, recovery}, std::pair{recovery, live},
                            std::pair{stagedRestore.stagingRoot, live},
                            std::pair{stagedRestore.stagingRoot, recovery}}) {
      bool overlap = false;
      if (!TryIsPathWithin(pair.first, pair.second, overlap) || overlap)
        return Failure(E_INVALIDARG, L"Restore staging, live data and recovery paths must be distinct trees.");
    }
    if (!RestorePathMissing(recovery))
      return Failure(HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS),
                     L"The pre-restore recovery folder already exists.");
    BY_HANDLE_FILE_INFORMATION information{};
    verified = OpenRestoreDirectory(stagedRestore.stagingRoot, stage, information, true);
    if (!verified.ok()) return verified;
    verified = OpenRestoreDirectory(stagedRestore.payloadDir, payload, information, true);
    if (!verified.ok()) return verified;
    verified = OpenRestoreDirectory(live.parent_path(), currentParent, information,false,false,false);
    if (!verified.ok()) return verified;
    verified = OpenRestoreDirectory(recovery.parent_path(), recoveryParent, information,false,false,false);
    if (!verified.ok()) return verified;
    // Directory read leases block namespace replacement. Native rename needs
    // write sharing, so keep destination parents nonempty as well: an empty
    // bound directory must not be converted into a junction in place.
    verified=KeepRestoreParentNonempty(live.parent_path(),currentParentSentinel);
    if(!verified.ok())return verified;
    verified=AllowRestoreParentWrites(live.parent_path(),currentParent);
    if(!verified.ok())return verified;
    if(live.parent_path()!=recovery.parent_path()) {
      verified=KeepRestoreParentNonempty(recovery.parent_path(),recoveryParentSentinel);
      if(!verified.ok())return verified;
    }
    verified=AllowRestoreParentWrites(recovery.parent_path(),recoveryParent);
    if(!verified.ok())return verified;
    // Win32 FileRenameInfo requires an absolute destination. Pin every real
    // ancestor so no intermediate junction/rename can redirect that pathname.
    for (const fs::path &parent : {live.parent_path(), recovery.parent_path(),
                                  stagedRestore.stagingRoot.parent_path()}) {
      fs::path ancestor = parent;
      for (;;) {
        auto handle = std::make_unique<RestoreHandle>();
        verified = OpenRestoreDirectory(ancestor, *handle, information);
        if (!verified.ok()) return verified;
        ancestry.push_back(std::move(handle));
        const fs::path next = ancestor.parent_path();
        if (next.empty() || next == ancestor) break;
        ancestor = next;
      }
    }
    const bool currentSitesExists = !RestorePathMissing(live);
    if (currentSitesExists) {
      verified = OpenRestoreDirectory(live, current, information, true);
      if (!verified.ok()) return verified;
    }
    // Root handles reject deletion/replacement while the safety guard runs;
    // ancestor handles keep absolute destinations bound to these parents.
    verified = VerifyRestoreSnapshot(stagedRestore, true);
    if (!verified.ok()) return verified;
    auto validatePathBudgets = [&]() -> Result {
    for (const fs::directory_entry &entry : fs::directory_iterator(stagedRestore.payloadDir)) {
      const std::wstring clientName = entry.path().filename().wstring();
      if (!IsRealDirectory(entry.path()) ||
          !Detail::IsValidClientDirectoryName(clientName)) {
        return Failure(
            E_INVALIDARG,
            L"The staged restore contains an invalid client directory.");
      }
      if (!Detail::IsNewClientTargetPathWithinLegacyBudget(
              live, clientName)) {
        return Failure(
            HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE),
            L"The client '" + clientName +
                L"' is too long for this ctSpaces data-folder location. "
                L"Current profile data was left unchanged.");
      }
      std::wstring pathBudgetError;
      if (!Detail::IsTreeTargetWithinLegacyPathBudget(
              entry.path(), live / clientName,
              &pathBudgetError)) {
        return Failure(
            HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE),
            L"The client '" + clientName +
                L"' contains data that would exceed the safe Windows path "
                L"budget at this ctSpaces data-folder location. " +
                pathBudgetError);
      }
    }

    if (currentSitesExists) {
      std::wstring recoveryPathBudgetError;
      if (!Detail::IsTreeTargetWithinLegacyPathBudget(
              live, recovery, &recoveryPathBudgetError)) {
        return Failure(
            HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE),
            L"The current Sites data would exceed the safe Windows path "
            L"budget in the pre-restore recovery folder. Current profile "
            L"data was left unchanged. " +
                recoveryPathBudgetError);
      }
    }
    return {};
    };
    verified = validatePathBudgets();
    if (!verified.ok()) return verified;
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
    // The callback can perform work or pump messages. Recheck its exact sealed
    // input and path budgets before either verified object is moved.
    verified = VerifyRestoreSnapshot(stagedRestore, true);
    if (!verified.ok()) return verified;
    verified = validatePathBudgets();
    if (!verified.ok()) return verified;
    if (!RestorePathMissing(recovery) || (!currentSitesExists && !RestorePathMissing(live)))
      return Failure(HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS), L"A restore destination changed during the final safety check.");
    if (currentSitesExists) {
      verified = RenameRestoreDirectory(current.value, recovery);
      if (!verified.ok()) return verified;
      previousMoved = true;
    }
    verified = RenameRestoreDirectory(payload.value, live);
    if (!verified.ok()) return rejectWithRollback(verified);
    payloadMoved = true;
    verified = VerifyInstalledRestore(stagedRestore, live);
    if (!verified.ok()) return rejectWithRollback(verified);
    Result result{};
    if (!CleanupBoundRestoreStage(stagedRestore, stage.value))
      result.details =
          L"Restore completed, but its temporary folder could not be removed.";
    return result;
  } catch (const std::exception &error) {
    return rejectWithRollback(Failure(E_FAIL, Widen(error.what())));
  }
}

} // namespace CtBackup
