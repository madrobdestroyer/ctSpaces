#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

// Restore-only Win32 I/O spelling. Keep this temporary spelling out of archive
// keys, owned object paths, relative inventories and final client path budgets.
namespace CtRestoreIo {
inline std::filesystem::path Path(const std::filesystem::path &path) {
  namespace fs = std::filesystem;
  const auto invalid = [&]() -> fs::path {
    throw fs::filesystem_error("Unsupported restore I/O path", path,
                              std::make_error_code(std::errc::invalid_argument));
  };
  std::wstring value = path.native();
  if (value.empty() || value.find(L'\0') != std::wstring::npos)
    return invalid();
  std::replace(value.begin(), value.end(), L'/', L'\\');
  const auto drive = [](wchar_t ch) {
    return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z');
  };
  const auto prefix = [&value](const wchar_t *text) {
    for (size_t i = 0; text[i]; ++i) {
      if (i >= value.size()) return false;
      wchar_t ch = value[i], wanted = text[i];
      if (ch >= L'a' && ch <= L'z') ch = static_cast<wchar_t>(ch - (L'a' - L'A'));
      if (wanted >= L'a' && wanted <= L'z') wanted = static_cast<wchar_t>(wanted - (L'a' - L'A'));
      if (ch != wanted) return false;
    }
    return true;
  };
  if (prefix(L"\\\\?\\UNC\\"))
    value = L"\\\\" + value.substr(8);
  else if (prefix(L"\\\\?\\")) {
    if (value.size() < 7 || !drive(value[4]) || value[5] != L':' || value[6] != L'\\')
      return invalid();
    value.erase(0, 4);
  }
  for (const auto &part : fs::path(value))
    if (part == L"..") return invalid();
  if (value.size() >= 3 && drive(value[0]) && value[1] == L':' && value[2] == L'\\') {
    value = fs::path(value).lexically_normal().native();
    value = L"\\\\?\\" + value;
  } else if (value.size() >= 2 && value[0] == L'\\' && value[1] == L'\\') {
    const size_t serverEnd = value.find(L'\\', 2);
    const size_t shareEnd = serverEnd == std::wstring::npos ? serverEnd : value.find(L'\\', serverEnd + 1);
    const std::wstring server = value.substr(2, serverEnd == std::wstring::npos ? 0 : serverEnd - 2);
    const std::wstring share = serverEnd == std::wstring::npos ? std::wstring{} :
        value.substr(serverEnd + 1, shareEnd == std::wstring::npos ? shareEnd : shareEnd - serverEnd - 1);
    if (server.empty() || share.empty() || server == L"." || server == L".." || server == L"?" ||
        share == L"." || share == L"..")
      return invalid();
    value = fs::path(value).lexically_normal().native();
    value = L"\\\\?\\UNC\\" + value.substr(2);
  } else return invalid();
  if (value.size() >= 32767)
    throw fs::filesystem_error("Restore I/O path is too long", path,
                              std::make_error_code(std::errc::filename_too_long));
  return fs::path(std::move(value));
}
} // namespace CtRestoreIo
