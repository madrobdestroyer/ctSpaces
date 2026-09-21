#pragma once

#include <algorithm>
#include <limits>
#include <string>
#include <string_view>

namespace sibling_stage_name {

inline constexpr std::wstring_view kLinkExtension = L".lnk";
inline constexpr size_t kFullGuidHexCharacters = 32;
inline constexpr std::wstring_view kSingleCharacterAlphabet =
    L"0123456789abcdefghijklmnopqrstuvwxyz_-~";

[[nodiscard]] inline bool IsLowerHexToken(
    std::wstring_view collisionToken) noexcept {
  if (collisionToken.empty())
    return false;
  for (const wchar_t character : collisionToken) {
    if (!((character >= L'0' && character <= L'9') ||
          (character >= L'a' && character <= L'f'))) {
      return false;
    }
  }
  return true;
}

// Builds a legal sibling component no longer than the destination component.
// A '~' prefix prevents DOS-device aliases whenever the budget permits it.
// Tiny budgets retain fewer random bits, but callers probe for absence and use
// no-replace moves, so a collision can only cause a clean retry or failure.
[[nodiscard]] inline std::wstring Build(
    std::wstring_view collisionToken, bool retainLinkExtension,
    size_t maximumComponentLength =
        (std::numeric_limits<size_t>::max)()) {
  if (!IsLowerHexToken(collisionToken))
    return {};

  const std::wstring_view extension =
      retainLinkExtension ? kLinkExtension : std::wstring_view{};
  if (maximumComponentLength <= extension.size())
    return {};

  const size_t baseBudget = maximumComponentLength - extension.size();
  std::wstring result;
  if (baseBudget > 1) {
    result.push_back(L'~');
    const size_t tokenLength =
        (std::min)(collisionToken.size(), baseBudget - 1);
    result.append(collisionToken.substr(0, tokenLength));
  } else {
    result.push_back(collisionToken.front());
  }
  result.append(extension);
  return result;
}

[[nodiscard]] inline std::wstring BuildSingleCharacter(
    size_t alphabetIndex, bool retainLinkExtension,
    size_t maximumComponentLength) {
  const std::wstring_view extension =
      retainLinkExtension ? kLinkExtension : std::wstring_view{};
  if (kSingleCharacterAlphabet.empty() ||
      maximumComponentLength < 1 + extension.size()) {
    return {};
  }
  std::wstring result(
      1, kSingleCharacterAlphabet[alphabetIndex %
                                  kSingleCharacterAlphabet.size()]);
  result.append(extension);
  return result;
}

} // namespace sibling_stage_name
