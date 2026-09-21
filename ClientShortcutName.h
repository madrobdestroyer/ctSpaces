#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <format>
#include <string>

namespace client_shortcut_name {

// Leave headroom below NTFS's 255 UTF-16-code-unit component limit. The
// Desktop directory can still be long, so keeping the component bounded also
// avoids needlessly consuming the legacy MAX_PATH budget used by some shell
// implementations.
inline constexpr size_t kMaxFileNameLength = 240;

inline uint64_t HashCaseInsensitive(const std::wstring &clientName) {
  uint64_t hash = 14695981039346656037ull;
  for (wchar_t ch : clientName) {
    hash ^= static_cast<uint64_t>(towlower(ch));
    hash *= 1099511628211ull;
  }
  return hash;
}

inline bool IsHighSurrogate(wchar_t ch) {
  return ch >= static_cast<wchar_t>(0xD800) &&
         ch <= static_cast<wchar_t>(0xDBFF);
}

inline bool IsLowSurrogate(wchar_t ch) {
  return ch >= static_cast<wchar_t>(0xDC00) &&
         ch <= static_cast<wchar_t>(0xDFFF);
}

inline std::wstring Build(const std::wstring &clientName,
                          const std::wstring &suffix,
                          size_t maximumFileNameLength) {
  maximumFileNameLength =
      (std::min)(maximumFileNameLength, kMaxFileNameLength);
  if (suffix.size() >= maximumFileNameLength)
    return L"";

  const size_t availableBaseLength = maximumFileNameLength - suffix.size();
  if (clientName.size() <= availableBaseLength)
    return clientName + suffix;

  const std::wstring hashToken =
      std::format(L"~{:016X}", HashCaseInsensitive(clientName));
  if (hashToken.size() > availableBaseLength)
    return L"";

  size_t prefixLength = availableBaseLength - hashToken.size();
  prefixLength = (std::min)(prefixLength, clientName.size());
  // Do not split a UTF-16 surrogate pair at the truncation boundary.
  if (prefixLength > 0 && prefixLength < clientName.size() &&
      IsHighSurrogate(clientName[prefixLength - 1]) &&
      IsLowSurrogate(clientName[prefixLength])) {
    --prefixLength;
  }
  return clientName.substr(0, prefixLength) + hashToken + suffix;
}

inline std::wstring Build(const std::wstring &clientName,
                          const std::wstring &suffix) {
  return Build(clientName, suffix, kMaxFileNameLength);
}

} // namespace client_shortcut_name
