#pragma once

#include <windows.h>
#include <limits>
#include <string_view>

namespace client_name_comparison {

inline int CompareIgnoringCase(std::wstring_view left,
                               std::wstring_view right) noexcept {
  if (left.empty() || right.empty())
    return left.empty() ? (right.empty() ? 0 : -1) : 1;
  if (left.size() <= static_cast<size_t>((std::numeric_limits<int>::max)()) &&
      right.size() <= static_cast<size_t>((std::numeric_limits<int>::max)())) {
    const int result = CompareStringOrdinal(
        left.data(), static_cast<int>(left.size()), right.data(),
        static_cast<int>(right.size()), TRUE);
    if (result != 0) return result - CSTR_EQUAL;
  }
  // Keep map ordering well-defined even for an invalid API length/input.
  const int result = left.compare(right);
  return result < 0 ? -1 : result > 0 ? 1 : 0;
}

// Client directories use Windows ordinal case folding. CRT _wcsicmp follows the
// C locale and can treat accented case variants as different client names.
// Ordinal comparison does not expand sharp-s to SS or normalize combining forms.
inline bool EqualIgnoringCase(std::wstring_view left,
                              std::wstring_view right) noexcept {
  if (left.empty() || right.empty()) return left.empty() && right.empty();
  if (left.size() > static_cast<size_t>((std::numeric_limits<int>::max)()) ||
      right.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) return false;
  return CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
                              right.data(), static_cast<int>(right.size()),
                              TRUE) == CSTR_EQUAL;
}

} // namespace client_name_comparison
