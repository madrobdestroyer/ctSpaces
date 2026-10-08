#pragma once

#include <windows.h>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

// Notes belong to the whole client container. Moving or backing up that
// container moves the notes with it; opening the editor never creates one.
namespace client_notes {
inline constexpr wchar_t kFileName[] = L"ctSpaces-client-notes.txt";
inline constexpr DWORD kMaxBytes = 128 * 1024;
inline std::mutex mutex;

enum class ReadStatus { Ok, Missing, Error };
struct ReadResult {
  ReadStatus status = ReadStatus::Error;
  std::wstring text;
};

inline bool SafeDirectory(const std::filesystem::path &root) {
  const DWORD attributes = GetFileAttributesW(root.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
         (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

inline bool SafeFileHandle(HANDLE file, DWORD &size) {
  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(file, &info) ||
      (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY |
                                FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
      info.nNumberOfLinks != 1 || info.nFileSizeHigh != 0 ||
      info.nFileSizeLow > kMaxBytes || GetFileType(file) != FILE_TYPE_DISK)
    return false;
  size = info.nFileSizeLow;
  return true;
}

inline bool DecodeUtf8(const std::string &bytes, std::wstring &text) {
  text.clear();
  if (bytes.empty())
    return true;
  if (bytes.find('\0') != std::string::npos)
    return false;
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         bytes.data(),
                                         static_cast<int>(bytes.size()),
                                         nullptr, 0);
  if (length <= 0)
    return false;
  text.resize(length);
  return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
                             static_cast<int>(bytes.size()), text.data(),
                             length) == length;
}

inline bool EncodeUtf8(std::wstring_view text, std::string &bytes) {
  bytes.clear();
  if (text.empty())
    return true;
  if (text.find(L'\0') != std::wstring_view::npos)
    return false;
  if (text.size() > kMaxBytes)
    return false;
  const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                         text.data(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
  if (length <= 0 || length > static_cast<int>(kMaxBytes))
    return false;
  bytes.resize(length);
  return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                             static_cast<int>(text.size()), bytes.data(),
                             length, nullptr, nullptr) == length;
}

inline ReadResult Read(const std::filesystem::path &root) {
  if (!SafeDirectory(root))
    return {};
  const auto path = root / kFileName;
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    return {error == ERROR_FILE_NOT_FOUND ? ReadStatus::Missing
                                         : ReadStatus::Error, {}};
  }
  DWORD size = 0;
  std::string bytes;
  bool safe = SafeFileHandle(file, size);
  if (safe) {
    bytes.resize(size);
    DWORD actual = 0;
    safe = size == 0 || (ReadFile(file, bytes.data(), size, &actual, nullptr) &&
                         actual == size);
  }
  safe = CloseHandle(file) != FALSE && safe;
  std::wstring text;
  if (!safe || !SafeDirectory(root) || !DecodeUtf8(bytes, text))
    return {};
  return {ReadStatus::Ok, std::move(text)};
}

// The expected snapshot prevents a stale editor from replacing notes changed
// by another process. A failed read is never treated as an empty note.
inline bool Write(const std::filesystem::path &root, std::wstring_view text,
                  const ReadResult &expected,
                  const std::function<bool()> &revalidate = {}) {
  std::lock_guard<std::mutex> lock(mutex);
  std::string bytes;
  if (!SafeDirectory(root) || !EncodeUtf8(text, bytes) ||
      (revalidate && !revalidate()))
    return false;
  const ReadResult current = Read(root);
  if (current.status == ReadStatus::Error ||
      current.status != expected.status || current.text != expected.text)
    return false;

  const auto path = root / kFileName;
  for (unsigned attempt = 0; attempt < 16; ++attempt) {
    const auto stage = root /
        (L".ctSpaces-notes-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt));
    HANDLE file = CreateFileW(stage.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL |
                                              FILE_FLAG_WRITE_THROUGH,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      if (GetLastError() == ERROR_FILE_EXISTS ||
          GetLastError() == ERROR_ALREADY_EXISTS)
        continue;
      return false;
    }
    DWORD written = 0;
    const bool saved = (bytes.empty() ||
                        (WriteFile(file, bytes.data(),
                                   static_cast<DWORD>(bytes.size()), &written,
                                   nullptr) && written == bytes.size())) &&
                       FlushFileBuffers(file) != FALSE;
    const bool closed = CloseHandle(file) != FALSE;
    const bool ready = saved && closed && SafeDirectory(root) &&
                       (!revalidate || revalidate()) &&
                       [&]() {
                         const ReadResult latest = Read(root);
                         return latest.status == expected.status &&
                                latest.text == expected.text;
                       }();
    const bool moved = ready &&
        MoveFileExW(stage.c_str(), path.c_str(),
                    MOVEFILE_WRITE_THROUGH |
                    (expected.status == ReadStatus::Missing
                         ? 0 : MOVEFILE_REPLACE_EXISTING));
    if (!moved) {
      DeleteFileW(stage.c_str());
      return false;
    }
    return true;
  }
  return false;
}
} // namespace client_notes
