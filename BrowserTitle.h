#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace browser_title {

inline bool EndsWith(std::wstring_view text, std::wstring_view suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline std::optional<std::wstring>
GetPageTitle(const std::wstring &currentTitle,
             const std::wstring &sessionLabel,
             const std::wstring &appAlias, bool isMicrosoftEdge) {
  const std::wstring appSuffix = L" - " + appAlias;
  const std::wstring clientPrefix = sessionLabel + L" - ";
  const std::wstring clientSuffix = L" - " + sessionLabel + appSuffix;

  if (currentTitle.starts_with(clientPrefix) &&
      EndsWith(currentTitle, appSuffix) &&
      currentTitle.size() > clientPrefix.size() + appSuffix.size()) {
    return currentTitle.substr(
        clientPrefix.size(),
        currentTitle.size() - clientPrefix.size() - appSuffix.size());
  }
  if (EndsWith(currentTitle, clientSuffix) &&
      currentTitle.size() > clientSuffix.size()) {
    return currentTitle.substr(0,
                               currentTitle.size() - clientSuffix.size());
  }

  // Edge adds a numbered profile label on some machines, but a normal
  // single-profile window often has only the product suffix. Some releases
  // place a zero-width space in the brand. Match only observed exact suffixes
  // so page text such as "Microsoft Malware Edge" is never mistaken for the
  // browser brand.
  constexpr std::array<std::wstring_view, 4> edgeBrandSuffixes = {
      L" - Microsoft Edge", L" - Microsoft\u200BEdge",
      L" - Microsoft \u200BEdge", L" - Microsoft\u200B Edge"};
  std::optional<std::wstring> edgeTitle;
  if (isMicrosoftEdge) {
    for (const std::wstring_view edgeBrandSuffix : edgeBrandSuffixes) {
      if (EndsWith(currentTitle, edgeBrandSuffix) &&
          currentTitle.size() > edgeBrandSuffix.size()) {
        edgeTitle = currentTitle.substr(
            0, currentTitle.size() - edgeBrandSuffix.size());
        break;
      }
    }
  }
  if (edgeTitle) {
    constexpr std::wstring_view profileMarker = L" - Profile ";
    const size_t marker = edgeTitle->rfind(profileMarker);
    if (marker != std::wstring::npos && marker > 0) {
      const size_t numberStart = marker + profileMarker.size();
      bool numberedProfile = numberStart < edgeTitle->size();
      for (size_t index = numberStart;
           numberedProfile && index < edgeTitle->size(); ++index) {
        numberedProfile = (*edgeTitle)[index] >= L'0' &&
                          (*edgeTitle)[index] <= L'9';
      }
      if (numberedProfile)
        return edgeTitle->substr(0, marker);
    }
    return edgeTitle;
  }

  constexpr std::array<std::wstring_view, 5> browserSuffixes = {
      L" - Google Chrome", L" - Brave", L" - Brave Browser",
      L" \u2014 Mozilla Firefox", L" - Mozilla Firefox"};
  for (const std::wstring_view browserSuffix : browserSuffixes) {
    if (EndsWith(currentTitle, browserSuffix) &&
        currentTitle.size() > browserSuffix.size()) {
      return currentTitle.substr(
          0, currentTitle.size() - browserSuffix.size());
    }
  }
  return std::nullopt;
}

inline std::wstring Format(const std::wstring &pageTitle,
                           const std::wstring &sessionLabel,
                           const std::wstring &appAlias, bool clientFirst) {
  if (clientFirst)
    return sessionLabel + L" - " + pageTitle + L" - " + appAlias;
  return pageTitle + L" - " + sessionLabel + L" - " + appAlias;
}

} // namespace browser_title
