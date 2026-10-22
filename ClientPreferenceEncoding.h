#pragma once
#include <string>
#include <string_view>

namespace client_preference_encoding {
// WinINI strips exactly one matching quote pair from a retrieved value.
// Double quotes cannot occur in a valid Windows client directory name. Use
// them only when the raw identity would otherwise be read as a quoted value;
// all ordinary names and browser-qualified tokens keep their existing bytes.
inline std::wstring ForIni(std::wstring_view value) {
  if (value.size() >= 2 && value.front() == L'\'' && value.back() == L'\'')
    return L"\"" + std::wstring(value) + L"\"";
  return std::wstring(value);
}
}
