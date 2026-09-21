#include "../ClientShortcutName.h"
#include "../BrowserTitle.h"
#include "../SiblingStageName.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool EndsWith(const std::wstring &value, const std::wstring &suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
             0;
}

bool HasUnpairedSurrogate(const std::wstring &value) {
  for (size_t index = 0; index < value.size(); ++index) {
    if (client_shortcut_name::IsHighSurrogate(value[index])) {
      if (index + 1 >= value.size() ||
          !client_shortcut_name::IsLowSurrogate(value[index + 1])) {
        return true;
      }
      ++index;
    } else if (client_shortcut_name::IsLowSurrogate(value[index])) {
      return true;
    }
  }
  return false;
}

} // namespace

int wmain() {
  struct BrowserTitleCase {
    const wchar_t *currentTitle;
    bool isMicrosoftEdge;
    std::optional<std::wstring> expectedPageTitle;
  };
  const std::wstring titleSession = L"Alpha [Microsoft Edge]";
  const std::vector<BrowserTitleCase> browserTitleCases = {
      {L"New tab - Microsoft Edge", true, L"New tab"},
      {L"Inbox - Profile 12 - Microsoft Edge", true, L"Inbox"},
      {L"Inbox - Work - Microsoft Edge", true, L"Inbox - Work"},
      {L"Dashboard - Microsoft\u200BEdge", true, L"Dashboard"},
      {L"Dashboard - Microsoft \u200BEdge", true, L"Dashboard"},
      {L"Dashboard - Microsoft\u200B Edge", true, L"Dashboard"},
      {L"Alpha [Microsoft Edge] - Portal - ctSpaces", true, L"Portal"},
      {L"Portal - Alpha [Microsoft Edge] - ctSpaces", true, L"Portal"},
      {L"Portal - Google Chrome", false, L"Portal"},
      {L"Portal - Brave Browser", false, L"Portal"},
      {L"Portal \u2014 Mozilla Firefox", false, L"Portal"},
      {L"Portal - Mozilla Firefox", false, L"Portal"},
      {L"Portal - Microsoft NotEdge", true, std::nullopt},
      {L"Portal - Microsoft Malware Edge", true, std::nullopt},
      {L" - Microsoft Edge", true, std::nullopt},
      {L"Portal - Microsoft", true, std::nullopt},
      {L"Portal - Microsoft Edge", false, std::nullopt}};
  for (const BrowserTitleCase &test : browserTitleCases) {
    const auto observed = browser_title::GetPageTitle(
        test.currentTitle, titleSession, L"ctSpaces",
        test.isMicrosoftEdge);
    if (observed != test.expectedPageTitle) {
      std::wcerr << L"Browser-title normalization failed for: "
                 << test.currentTitle << std::endl;
      return 1;
    }
  }
  const std::wstring pageFirst = browser_title::Format(
      L"Portal", titleSession, L"ctSpaces", false);
  const std::wstring clientFirst = browser_title::Format(
      L"Portal", titleSession, L"ctSpaces", true);
  if (pageFirst != L"Portal - Alpha [Microsoft Edge] - ctSpaces" ||
      clientFirst != L"Alpha [Microsoft Edge] - Portal - ctSpaces" ||
      browser_title::GetPageTitle(pageFirst, titleSession, L"ctSpaces",
                                  true) !=
          std::optional<std::wstring>(L"Portal") ||
      browser_title::GetPageTitle(clientFirst, titleSession, L"ctSpaces",
                                  true) !=
          std::optional<std::wstring>(L"Portal")) {
    std::wcerr << L"Live title-preference reformatting is not stable."
               << std::endl;
    return 1;
  }

  const std::vector<std::wstring> suffixes = {
      L" - Microsoft Edge - ctSpaces.lnk",
      L" - Google Chrome - ctSpaces.lnk",
      L" - Brave - ctSpaces.lnk",
      L" - Mozilla Firefox - ctSpaces.lnk",
      L" - ctSpaces.lnk"};
  const std::wstring maxClient(240, L'A');
  std::wstring collidingPrefixClient = maxClient;
  collidingPrefixClient.back() = L'B';

  for (const auto &suffix : suffixes) {
    const std::wstring first =
        client_shortcut_name::Build(maxClient, suffix);
    const std::wstring repeat =
        client_shortcut_name::Build(maxClient, suffix);
    const std::wstring distinct =
        client_shortcut_name::Build(collidingPrefixClient, suffix);
    if (first.empty() ||
        first.size() > client_shortcut_name::kMaxFileNameLength ||
        !EndsWith(first, suffix) || first != repeat || first == distinct) {
      std::wcerr << L"240-character boundary/hash check failed for suffix: "
                 << suffix << L'\n';
      return 1;
    }
  }

  const std::wstring guidToken = L"0123456789abcdef0123456789abcdef";
  for (size_t componentBudget = 1; componentBudget <= 80;
       ++componentBudget) {
    const std::wstring stageName = sibling_stage_name::Build(
        guidToken, false, componentBudget);
    if (stageName.empty() || stageName.size() > componentBudget) {
      std::wcerr << L"A constrained directory-stage component exceeded its "
                    L"source-name budget.\n";
      return 1;
    }
  }
  if (sibling_stage_name::Build(guidToken, false, 1).size() != 1 ||
      sibling_stage_name::Build(guidToken, false, 2).size() != 2 ||
      sibling_stage_name::Build(guidToken, false, 33) !=
          L"~" + guidToken) {
    std::wcerr << L"Short/deep-path directory staging lost its bounded-name "
                  L"invariant or its full 128-bit token.\n";
    return 1;
  }
  for (size_t componentBudget = 5; componentBudget <= 80;
       ++componentBudget) {
    const std::wstring linkStageName = sibling_stage_name::Build(
        guidToken, true, componentBudget);
    if (linkStageName.empty() || linkStageName.size() > componentBudget ||
        !EndsWith(linkStageName, L".lnk")) {
      std::wcerr << L"Constrained ShellLink staging lost its .lnk extension "
                    L"or exceeded the destination-name budget.\n";
      return 1;
    }
  }
  if (!sibling_stage_name::Build(guidToken, true, 4).empty() ||
      sibling_stage_name::Build(guidToken, true, 37) !=
          L"~" + guidToken + L".lnk" ||
      !sibling_stage_name::Build(L"not-hex", false, 20).empty()) {
    std::wcerr << L"Sibling-stage extension, full-token, or legal-alphabet "
                  L"validation failed.\n";
    return 1;
  }

  constexpr size_t simulatedDeepParentLength = 107;
  const std::wstring oneCharacterStage =
      sibling_stage_name::Build(guidToken, false, 1);
  if (simulatedDeepParentLength + 1 + oneCharacterStage.size() >
      simulatedDeepParentLength + 2) {
    std::wcerr << L"A one-character client at a deep data path gained path "
                  L"length while staging.\n";
    return 1;
  }
  std::wstring observedSingleCharacterCandidates;
  constexpr size_t randomizedStart = 17;
  for (size_t attempt = 0;
       attempt < sibling_stage_name::kSingleCharacterAlphabet.size();
       ++attempt) {
    const std::wstring candidate =
        sibling_stage_name::BuildSingleCharacter(
            randomizedStart + attempt, false, 1);
    if (candidate.size() != 1 ||
        observedSingleCharacterCandidates.find(candidate.front()) !=
            std::wstring::npos) {
      std::wcerr << L"The one-character staging namespace was not exhausted "
                    L"exactly once in randomized rotation.\n";
      return 1;
    }
    observedSingleCharacterCandidates.push_back(candidate.front());
  }
  if (observedSingleCharacterCandidates.size() !=
          sibling_stage_name::kSingleCharacterAlphabet.size() ||
      observedSingleCharacterCandidates.find(L'g') == std::wstring::npos ||
      sibling_stage_name::BuildSingleCharacter(0, true, 5).size() != 5 ||
      !EndsWith(sibling_stage_name::BuildSingleCharacter(0, true, 5),
                L".lnk")) {
    std::wcerr << L"The one-character fallback omitted safe names beyond "
                  L"0-f or failed to retain .lnk.\n";
    return 1;
  }
  const std::wstring allButGOccupied =
      L"0123456789abcdefhijklmnopqrstuvwxyz_-~";
  wchar_t selectedCandidate = L'\0';
  for (size_t attempt = 0;
       attempt < sibling_stage_name::kSingleCharacterAlphabet.size();
       ++attempt) {
    const wchar_t candidate = sibling_stage_name::BuildSingleCharacter(
                                  randomizedStart + attempt, false, 1)
                                  .front();
    if (allButGOccupied.find(candidate) == std::wstring::npos) {
      selectedCandidate = candidate;
      break;
    }
  }
  if (selectedCandidate != L'g') {
    std::wcerr << L"The exhaustive one-character fallback could not use the "
                  L"only unoccupied safe candidate.\n";
    return 1;
  }

  const std::wstring shortClient = L"Alpha";
  const std::wstring exactClientSuffix = L".lnk";
  if (client_shortcut_name::Build(shortClient, exactClientSuffix) !=
      L"Alpha.lnk") {
    std::wcerr << L"A normal client-only shortcut did not retain the exact "
                  L"client title.\n";
    return 1;
  }
  const std::wstring shortSuffix = L" - ctSpaces.lnk";
  if (client_shortcut_name::Build(shortClient, shortSuffix) !=
      shortClient + shortSuffix) {
    std::wcerr << L"Short names no longer preserve their readable filename.\n";
    return 1;
  }

  // Historical path-budgeted names may have been created while the Desktop
  // lived at any path length. Every budget capable of retaining the complete
  // 64-bit hash must therefore remain deterministically reconstructible for
  // rename/delete discovery after Desktop redirection.
  for (const auto &suffix : suffixes) {
    constexpr size_t hashTokenLength = 17;
    for (size_t historicalBudget = suffix.size() + hashTokenLength;
         historicalBudget <= client_shortcut_name::kMaxFileNameLength;
         ++historicalBudget) {
      const std::wstring first = client_shortcut_name::Build(
          maxClient, suffix, historicalBudget);
      const std::wstring repeat = client_shortcut_name::Build(
          maxClient, suffix, historicalBudget);
      const std::wstring distinct = client_shortcut_name::Build(
          collidingPrefixClient, suffix, historicalBudget);
      if (first.empty() || first.size() > historicalBudget ||
          !EndsWith(first, suffix) || first != repeat || first == distinct) {
        std::wcerr << L"Historical shortcut budget reconstruction failed at "
                   << historicalBudget << L" code units for suffix: " << suffix
                   << L'\n';
        return 1;
      }
    }
  }

  constexpr size_t pathDerivedBudget = 80;
  for (const auto &suffix : suffixes) {
    const std::wstring first = client_shortcut_name::Build(
        maxClient, suffix, pathDerivedBudget);
    const std::wstring repeat = client_shortcut_name::Build(
        maxClient, suffix, pathDerivedBudget);
    const std::wstring distinct = client_shortcut_name::Build(
        collidingPrefixClient, suffix, pathDerivedBudget);
    if (first.empty() || first.size() > pathDerivedBudget ||
        !EndsWith(first, suffix) || first != repeat || first == distinct) {
      std::wcerr << L"Path-derived shortcut budget check failed for suffix: "
                 << suffix << L'\n';
      return 1;
    }
    if (!client_shortcut_name::Build(
             maxClient, suffix, suffix.size() + 16)
             .empty()) {
      std::wcerr << L"A truncated collision hash was accepted.\n";
      return 1;
    }
    const std::wstring hashOnly = client_shortcut_name::Build(
        maxClient, suffix, suffix.size() + 17);
    if (hashOnly.size() != suffix.size() + 17 ||
        !EndsWith(hashOnly, suffix)) {
      std::wcerr << L"A complete hash-only shortcut name was rejected.\n";
      return 1;
    }
  }

  for (const auto &suffix : suffixes) {
    const size_t hashTokenLength = 17;
    const size_t prefixLength =
        client_shortcut_name::kMaxFileNameLength - suffix.size() -
        hashTokenLength;
    std::wstring surrogateBoundary(prefixLength - 1, L'A');
    surrogateBoundary.push_back(static_cast<wchar_t>(0xD83D));
    surrogateBoundary.push_back(static_cast<wchar_t>(0xDE80));
    surrogateBoundary.append(240 - surrogateBoundary.size(), L'B');
    const std::wstring result =
        client_shortcut_name::Build(surrogateBoundary, suffix);
    const std::wstring base =
        result.substr(0, result.size() - suffix.size());
    if (result.size() > client_shortcut_name::kMaxFileNameLength ||
        HasUnpairedSurrogate(base)) {
      std::wcerr << L"UTF-16 boundary check failed for suffix: " << suffix
                 << L'\n';
      return 1;
    }

    const size_t limitedPrefixLength =
        pathDerivedBudget - suffix.size() - hashTokenLength;
    std::wstring limitedBoundary(limitedPrefixLength - 1, L'A');
    limitedBoundary.push_back(static_cast<wchar_t>(0xD83D));
    limitedBoundary.push_back(static_cast<wchar_t>(0xDE80));
    limitedBoundary.append(240 - limitedBoundary.size(), L'B');
    const std::wstring limitedResult = client_shortcut_name::Build(
        limitedBoundary, suffix, pathDerivedBudget);
    const std::wstring limitedBase =
        limitedResult.substr(0, limitedResult.size() - suffix.size());
    if (limitedResult.size() > pathDerivedBudget ||
        HasUnpairedSurrogate(limitedBase)) {
      std::wcerr
          << L"Path-derived UTF-16 boundary check failed for suffix: "
          << suffix << L'\n';
      return 1;
    }
  }

  std::wcout
      << L"Client-only, historical shortcut, and sibling-stage filename "
         L"boundary tests passed.\n";
  return 0;
}
