#include "../BrowserTitle.h"
#include <iostream>
#include <stdexcept>
#include <vector>

static unsigned checks = 0;
static void Check(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

int wmain() {
  try {
    const std::wstring session = L"Alpha [Microsoft Edge]";
    const std::vector<std::wstring> pages = {
      L"Portal", session + L" - Portal", L"Portal - " + session,
      session + L" - Portal - " + session,
      L"Page - ctSpaces - " + session + L" - ctSpaces",
      L"\u65e5\u672c-\U0001f680 - " + session,
      std::wstring(2048, L'x') + L" - " + session
    };
    unsigned iterations = 0;
    for (const bool edge : {false, true}) {
      const std::wstring brand = edge ? L" - Microsoft Edge" : L" - Google Chrome";
      for (const auto &page : pages) {
        for (const bool initialClientFirst : {false, true}) {
          browser_title::AppliedTitleState state;
          std::wstring current = page + brand;
          auto resolved = state.Resolve(current, session, L"ctSpaces", edge);
          Check(resolved && *resolved == page, "a fresh native browser title supplies exact original page text");
          current = browser_title::Format(*resolved, session, L"ctSpaces", initialClientFirst);
          state.RememberApplied(*resolved, current, session, L"ctSpaces", edge);
          for (unsigned repeat = 0; repeat < 120; ++repeat) {
            resolved = state.Resolve(current, session, L"ctSpaces", edge);
            Check(resolved && *resolved == page,
                  "watcher repetition and preference changes preserve ambiguous original page text");
            const auto desired = browser_title::Format(*resolved, session, L"ctSpaces", repeat % 3 == 0);
            state.RememberApplied(*resolved, desired, session, L"ctSpaces", edge);
            current = desired;
            ++iterations;
          }
          const auto changed = state.Resolve(L"New navigation" + brand, session, L"ctSpaces", edge);
          Check(changed && *changed == L"New navigation", "browser navigation replaces cached page text");
          Check(!state.Resolve(L"Unknown native title", session, L"ctSpaces", edge),
                "unrecognized external titles are not replaced by stale cached text");
          const std::wstring otherSession = L"Beta [Microsoft Edge]";
          Check(state.Resolve(current, otherSession, L"ctSpaces", edge) ==
                    browser_title::GetPageTitle(current, otherSession, L"ctSpaces", edge),
                "client context changes never reuse another client's cached page");
          Check(state.Resolve(current, session, L"NewAlias", edge) ==
                    browser_title::GetPageTitle(current, session, L"NewAlias", edge),
                "app alias changes never reuse stale cached page context");
          Check(state.Resolve(current, session, L"ctSpaces", !edge) ==
                    browser_title::GetPageTitle(current, session, L"ctSpaces", !edge),
                "browser context changes never reuse stale cached page context");
        }
      }
    }
    browser_title::AppliedTitleState unconfirmed;
    const std::wstring native = L"First page - Microsoft Edge";
    const auto original = unconfirmed.Resolve(native, session, L"ctSpaces", true);
    Check(original && *original == L"First page", "unconfirmed fixture resolves original page");
    // A rejected WM_SETTEXT must leave the state unrecorded.
    const auto unchanged = unconfirmed.Resolve(native, session, L"ctSpaces", true);
    Check(unchanged == original, "a failed title write cannot change the next observed page");
    std::cout << checks << " browser title state checks passed; " << iterations
              << " repeated-format/preference-toggle iterations\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
