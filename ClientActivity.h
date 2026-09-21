#pragma once

#include <windows.h>
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

// Deliberate application activity, never NTFS access/modified timestamps.
// Kept at the whole-client root so rename preserves it and deletion removes it.
namespace client_activity {
inline constexpr wchar_t kFileName[] = L"ctSpaces-client-activity";
struct Record {
  unsigned long long time = 0;
  bool baseline = false; // Pre-upgrade clients have no historical open date.
  bool operator==(const Record &) const = default;
};
inline std::mutex mutex;

inline unsigned long long ToTicks(const FILETIME &value) {
  return (static_cast<unsigned long long>(value.dwHighDateTime) << 32) |
         value.dwLowDateTime;
}
inline FILETIME ToFileTime(unsigned long long value) {
  return {static_cast<DWORD>(value), static_cast<DWORD>(value >> 32)};
}
inline unsigned long long Now() {
  FILETIME value{};
  GetSystemTimeAsFileTime(&value);
  return ToTicks(value);
}
inline unsigned long long ThreeMonthsBefore(unsigned long long now) {
  SYSTEMTIME date{};
  const FILETIME value = ToFileTime(now);
  if (!FileTimeToSystemTime(&value, &date) || date.wYear < 1602)
    return 0;
  const unsigned monthIndex = date.wYear * 12u + date.wMonth - 1u - 3u;
  date.wYear = static_cast<WORD>(monthIndex / 12u);
  date.wMonth = static_cast<WORD>(monthIndex % 12u + 1u);
  constexpr WORD days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  WORD maximum = days[date.wMonth - 1];
  if (date.wMonth == 2 && date.wYear % 4 == 0 &&
      (date.wYear % 100 != 0 || date.wYear % 400 == 0))
    maximum = 29;
  date.wDay = (std::min)(date.wDay, maximum);
  FILETIME cutoff{};
  return SystemTimeToFileTime(&date, &cutoff) ? ToTicks(cutoff) : 0;
}
inline bool IsInactive(const Record &record, unsigned long long now) {
  return record.time != 0 && record.time <= now &&
         record.time <= ThreeMonthsBefore(now);
}
inline std::string Encode(const Record &record) {
  return std::string("ctSpaces-activity=1\r\n") +
         (record.baseline ? "baseline=" : "opened=") +
         std::to_string(record.time) + "\r\n";
}
inline std::optional<Record> Decode(const std::string &text) {
  const std::string prefix = "ctSpaces-activity=1\r\n";
  if (!text.starts_with(prefix) || !text.ends_with("\r\n"))
    return std::nullopt;
  const size_t start = prefix.size();
  const bool baseline = text.compare(start, 9, "baseline=") == 0;
  const bool opened = text.compare(start, 7, "opened=") == 0;
  if (!baseline && !opened)
    return std::nullopt;
  const char *first = text.data() + start + (baseline ? 9 : 7);
  const char *last = text.data() + text.size() - 2;
  if (first >= last)
    return std::nullopt;
  Record result{0, baseline};
  const auto parsed = std::from_chars(first, last, result.time);
  SYSTEMTIME date{};
  const FILETIME value = ToFileTime(result.time);
  if (parsed.ec != std::errc{} || parsed.ptr != last ||
      !FileTimeToSystemTime(&value, &date) || date.wYear < 1970)
    return std::nullopt;
  return result;
}
inline bool SafeDirectory(const std::filesystem::path &root) {
  const DWORD attributes = GetFileAttributesW(root.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
         (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}
inline std::optional<Record> Read(const std::filesystem::path &root) {
  if (!SafeDirectory(root))
    return std::nullopt;
  const auto path = root / kFileName;
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING,
                            FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return std::nullopt;
  BY_HANDLE_FILE_INFORMATION info{};
  char buffer[96]{};
  DWORD read = 0;
  const bool safe = GetFileInformationByHandle(file, &info) &&
      (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY |
                               FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
      info.nNumberOfLinks == 1 && info.nFileSizeHigh == 0 &&
      info.nFileSizeLow < sizeof(buffer) &&
      ReadFile(file, buffer, info.nFileSizeLow, &read, nullptr) &&
      read == info.nFileSizeLow;
  CloseHandle(file);
  return safe ? Decode(std::string(buffer, read)) : std::nullopt;
}
inline bool Write(const std::filesystem::path &root, const Record &record,
                  bool onlyIfMissing = false) {
  std::lock_guard<std::mutex> lock(mutex);
  if (!SafeDirectory(root))
    return false;
  const auto path = root / kFileName;
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES) {
    if (onlyIfMissing)
      return Read(root).has_value(); // Never reset a known or corrupt history.
    if ((attributes & (FILE_ATTRIBUTE_DIRECTORY |
                       FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
      return false;
  } else if (GetLastError() != ERROR_FILE_NOT_FOUND) {
    return false;
  }
  const std::string contents = Encode(record);
  for (unsigned attempt = 0; attempt < 16; ++attempt) {
    const auto stage = root /
        (L".ctSpaces-activity-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt));
    HANDLE file = CreateFileW(stage.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL |
                              FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      if (GetLastError() == ERROR_FILE_EXISTS ||
          GetLastError() == ERROR_ALREADY_EXISTS)
        continue;
      return false;
    }
    DWORD written = 0;
    const bool saved = WriteFile(file, contents.data(),
                                 static_cast<DWORD>(contents.size()),
                                 &written, nullptr) &&
        written == contents.size() && FlushFileBuffers(file);
    const bool closed = CloseHandle(file) != FALSE;
    const bool moved = saved && closed && SafeDirectory(root) &&
        MoveFileExW(stage.c_str(), path.c_str(),
                    MOVEFILE_WRITE_THROUGH |
                    (onlyIfMissing ? 0 : MOVEFILE_REPLACE_EXISTING));
    if (!moved) {
      DeleteFileW(stage.c_str());
      return false;
    }
    return Read(root) == std::optional<Record>(record);
  }
  return false;
}
} // namespace client_activity
