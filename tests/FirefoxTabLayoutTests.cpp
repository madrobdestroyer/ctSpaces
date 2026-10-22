#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../FirefoxPreferences.h"
#include "RetainedFixture.h"
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
static unsigned checks = 0, scenarios = 0;
static unsigned shows = 0, focuses = 0, foregrounds = 0;
static void Check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
static std::string Read(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Cannot read retained fixture");
  return {std::istreambuf_iterator<char>(input), {}};
}
static void Write(const fs::path &path, const std::string &bytes) {
  std::ofstream output(path, std::ios::binary); output << bytes; output.close();
  if (!output) throw std::runtime_error("Cannot write retained fixture");
}
static BY_HANDLE_FILE_INFORMATION Identity(const fs::path &path) {
  HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  BY_HANDLE_FILE_INFORMATION value{};
  const bool valid = file != INVALID_HANDLE_VALUE && GetFileInformationByHandle(file, &value);
  if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
  if (!valid) throw std::runtime_error("Cannot identify retained fixture");
  return value;
}
static bool SameFile(const BY_HANDLE_FILE_INFORMATION &a, const BY_HANDLE_FILE_INFORMATION &b) {
  return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh &&
      a.nFileIndexLow == b.nFileIndexLow && CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) == 0;
}
static void CALLBACK Observe(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG, DWORD, DWORD) {
  DWORD pid = 0;
  if (!window || !GetWindowThreadProcessId(window, &pid) || pid != GetCurrentProcessId()) return;
  if (event == EVENT_OBJECT_SHOW && object == OBJID_WINDOW) ++shows;
  if (event == EVENT_OBJECT_FOCUS) ++focuses;
  if (event == EVENT_SYSTEM_FOREGROUND) ++foregrounds;
}
static const std::string onNone =
    "// ctSpaces tab-layout begin v1; separator=none\r\n"
    "user_pref(\"sidebar.revamp\", true);\r\n"
    "user_pref(\"sidebar.verticalTabs\", true);\r\n"
    "// ctSpaces tab-layout end v1\r\n";
static const std::string offNone =
    "// ctSpaces tab-layout begin v1; separator=none\r\n"
    "user_pref(\"sidebar.verticalTabs\", false);\r\n"
    "// ctSpaces tab-layout end v1\r\n";
int wmain(int argc, wchar_t **argv) {
  std::vector<HWINEVENTHOOK> hooks;
  fs::path fixture;
  try {
    if (argc > 2) return 2;
    for (DWORD event : {EVENT_SYSTEM_FOREGROUND, EVENT_OBJECT_SHOW, EVENT_OBJECT_FOCUS}) {
      const auto hook = SetWinEventHook(event, event, nullptr, Observe,
          GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
      if (!hook) throw std::runtime_error("Cannot observe native background guards");
      hooks.push_back(hook);
    }
    fixture = retained_fixture::CreateRoot(argc == 2 ? fs::absolute(argv[1]) :
        fs::current_path() / L"build" / L"qa-firefox-tabs", L"fixture");
    const auto other = fixture / L"other-client";
    fs::create_directory(other);
    const std::string otherBytes = "user_pref(\"sidebar.verticalTabs\", false);\r\nPRIVATE OTHER CLIENT\r\n";
    Write(other / L"user.js", otherBytes);
    const auto profile = [&]() { auto p = fixture / std::to_wstring(++scenarios); fs::create_directory(p); return p; };
    const std::vector<std::string> originals = {
      "",
      "\n",
      "\r\n",
      "// original no final newline",
      "user_pref(\"browser.startup.page\", 3);\r\nuser_pref(\"sidebar.revamp\", false);\r\n",
      "user_pref(\"sidebar.verticalTabs\", false);\r\nuser_pref('sidebar.verticalTabs', true);\r\n",
      "user_pref(\"sidebar\\u002everticalTabs\", false);\r\nuser_pref(\"sidebar.verticaltabs\", true);\r\n",
      "// Example: user_pref(\"sidebar.verticalTabs\", false);\r\n",
      "/*\r\n// ctSpaces tab-layout begin v1; separator=none\r\n// ctSpaces tab-layout end v1\r\n*/\r\n",
      "user_pref(\"example\", \"// ctSpaces tab-layout begin v1; separator=none\\n// ctSpaces tab-layout end v1\");\r\n",
      "user_pref(\"example\", \"literal\n// ctSpaces tab-layout end v1\ntext\");\n",
      "user_pref(\"x\", true); // ctSpaces tab-layout end v1\r\n",
      "# // ctSpaces tab-layout begin v1; separator=none\nuser_pref('custom', 'unchanged');\n",
      "\xef\xbb\xbfuser_pref(\"custom.unicode\", \"\xc3\xa9\");\n",
      "user_pref(\"own\", true);\n// LF no final newline",
      "user_pref(\"own\", true);\r"
    };
    for (const auto &original : originals) {
      const auto p = profile();
      Write(p / L"user.js", original);
      std::string expected;
      Check(firefox_preferences::UpdateTabLayoutPreferenceText(original, true, expected), "valid user source accepts a managed ON block");
      Check(expected.starts_with(original), "all original bytes including duplicate own preferences remain intact before override");
      Check(expected.find("user_pref(\"sidebar.revamp\", true);") != std::string::npos &&
          expected.find("user_pref(\"sidebar.verticalTabs\", true);") != std::string::npos, "ON writes Boolean orientation and dependency values");
      Check(firefox_preferences::UpdateTabLayoutPreferenceFile(p, true), "native atomic ON publication succeeds");
      Check(Read(p / L"user.js") == expected, "native publication equals the complete expected user-plus-managed bytes");
      const auto identity = Identity(p / L"user.js");
      Check(firefox_preferences::UpdateTabLayoutPreferenceFile(p, true), "repeated ON succeeds");
      Check(Read(p / L"user.js") == expected && SameFile(identity, Identity(p / L"user.js")), "idempotent mode does not replace the file or change its timestamp");
      Check(firefox_preferences::UpdateTabLayoutPreferenceFile(p, false), "explicit horizontal mode succeeds");
      const auto horizontal = Read(p / L"user.js");
      const size_t lf = horizontal.find('\n');
      Check(horizontal.starts_with(original) && horizontal.ends_with("// ctSpaces tab-layout end v1" +
          std::string(lf != std::string::npos && (lf == 0 || horizontal[lf - 1] != '\r') ? "\n" : "\r\n")), "OFF preserves user source and a complete terminal block");
      const auto marker = horizontal.rfind("// ctSpaces tab-layout begin v1;");
      Check(marker != std::string::npos && horizontal.substr(marker).find("sidebar.revamp") == std::string::npos &&
          horizontal.substr(marker).find("user_pref(\"sidebar.verticalTabs\", false);") != std::string::npos, "OFF changes orientation only and does not disable user sidebar tools");
      Check(firefox_preferences::UpdateTabLayoutPreferenceFile(p, std::nullopt), "Keep browser choice releases the managed override");
      Check(Read(p / L"user.js") == original, "ON to OFF to release restores every original byte, including missing final newline");
      Check(Read(other / L"user.js") == otherBytes, "another client stays byte-exact through ON/OFF/release");
      Check(std::distance(fs::directory_iterator(p), fs::directory_iterator{}) == 1, "successful publication leaves no staging files");
    }
    {
      const auto p = profile();
      std::wstring error = L"stale";
      Check(firefox_preferences::UpdateTabLayoutPreferenceFile(p, std::nullopt, &error) && error.empty(), "missing Keep preference is an unchanged success");
      Check(fs::is_empty(p), "Keep does not create an empty user.js");
      Check(firefox_preferences::UpdateTabLayoutPreferenceFile(p, true) && Read(p / L"user.js") == onNone, "a new profile has the exact standalone ON block");
      Check(firefox_preferences::UpdateTabLayoutPreferenceFile(p, false) && Read(p / L"user.js") == offNone, "a new explicit OFF block has no sidebar dependency override");
      Check(firefox_preferences::UpdateTabLayoutPreferenceFile(p, std::nullopt) && Read(p / L"user.js").empty(), "release removes only the new managed block");
    }
    {
      const auto p = profile();
      const std::string own = "user_pref(\"sidebar.verticalTabs\", false);\r\n";
      const std::string later = "user_pref(\"sidebar.verticalTabs\", false);\r\nuser_pref(\"later.own\", true);\r\n";
      Write(p / L"user.js", own + onNone + later);
      Check(firefox_preferences::UpdateTabLayoutPreferenceFile(p, true), "existing owned block before later user rows updates safely");
      Check(Read(p / L"user.js") == own + later + onNone, "force moves its exact block last without changing either user-owned section");
      Check(firefox_preferences::UpdateTabLayoutPreferenceFile(p, std::nullopt) && Read(p / L"user.js") == own + later, "release retains both original and later user rows");
    }
    {
      const auto p = profile();
      const std::string own = "// user comment originally without newline";
      Write(p / L"user.js", own);
      Check(firefox_preferences::UpdateTabLayoutPreferenceFile(p, true), "unterminated final user comment is safely separated from ON block");
      Check(firefox_preferences::UpdateStartupPreferenceFile(p, true), "existing startup helper can independently append after the managed block");
      Check(firefox_preferences::UpdateTabLayoutPreferenceFile(p, std::nullopt), "release handles a subsequently appended startup preference");
      Check(Read(p / L"user.js") == own + "\r\nuser_pref(\"browser.startup.page\", 3);\r\n", "release keeps a separator so the user's comment cannot swallow the later startup row");
    }
    const std::vector<std::string> malformed = {
      "// ctSpaces tab-layout end v1\r\n",
      "// ctSpaces tab-layout begin v1; separator=none\r\n",
      "// ctSpaces tab-layout begin v2; separator=none\r\n",
      "// ctSpaces tab-layout begin v1; separator=unknown\r\n",
      "// ctSpaces tab-layout begin v1; separator=crlf\r\n" + offNone,
      " " + onNone,
      onNone + offNone,
      "// ctSpaces tab-layout begin v1; separator=none\r\nuser_pref(\"custom.own\", true);\r\n// ctSpaces tab-layout end v1\r\n",
      "// ctSpaces tab-layout begin v1; separator=none\r\nuser_pref(\"sidebar.verticalTabs\", \"false\");\r\n// ctSpaces tab-layout end v1\r\n",
      "/* unterminated comment",
      "user_pref(\"example\", \"unterminated"
    };
    for (const auto &original : malformed) {
      const auto p = profile(); Write(p / L"user.js", original);
      for (std::optional<bool> mode : {std::optional<bool>(true), std::optional<bool>(false), std::optional<bool>()}) {
        Check(!firefox_preferences::UpdateTabLayoutPreferenceFile(p, mode), "malformed or ambiguous marker ownership refuses mutation");
        Check(Read(p / L"user.js") == original, "refusal preserves complete original file bytes");
      }
      Check(std::distance(fs::directory_iterator(p), fs::directory_iterator{}) == 1, "parse refusal does not create staging files");
    }
    {
      const auto p = profile(); Write(p / L"user.js", onNone);
      HANDLE locked = CreateFileW((p / L"user.js").c_str(), GENERIC_READ, FILE_SHARE_READ,
          nullptr, OPEN_EXISTING, 0, nullptr);
      Check(locked != INVALID_HANDLE_VALUE, "real no-delete-sharing publication lock acquired");
      Check(!firefox_preferences::UpdateTabLayoutPreferenceFile(p, false), "real locked publication refuses OFF atomically");
      Check(!firefox_preferences::UpdateTabLayoutPreferenceFile(p, std::nullopt), "real locked publication refuses release atomically");
      CloseHandle(locked);
      Check(Read(p / L"user.js") == onNone && std::distance(fs::directory_iterator(p), fs::directory_iterator{}) == 1, "both failed publications preserve full ON file without staging residue");
      Check(firefox_preferences::UpdateTabLayoutPreferenceFile(p, false) && Read(p / L"user.js") == offNone, "retry after native lock release succeeds");
    }
    {
      const auto p = profile(); const std::string full(firefox_preferences::kMaximumUserJsBytes, ' '); Write(p / L"user.js", full);
      Check(!firefox_preferences::UpdateTabLayoutPreferenceFile(p, true), "an oversized appended ON block is refused before publication");
      Check(Read(p / L"user.js") == full, "size-bound refusal keeps the full original bytes");
      std::string refused = "unchanged output";
      Check(!firefox_preferences::UpdateTabLayoutPreferenceText(full, true, refused) && refused == "unchanged output", "a refused text update also leaves the caller's output unchanged");
      Check(firefox_preferences::UpdateTabLayoutPreferenceFile(p, std::nullopt), "Keep does not grow an exactly full unowned file");
      const std::string exact(firefox_preferences::kMaximumUserJsBytes - onNone.size() - 2, ' ');
      Write(p / L"user.js", exact);
      Check(firefox_preferences::UpdateTabLayoutPreferenceFile(p, true), "an ON block fitting exactly within the input bound succeeds");
      Check(Read(p / L"user.js").size() == firefox_preferences::kMaximumUserJsBytes, "successful exact-bound output remains readable by the next launch");
      Check(firefox_preferences::UpdateTabLayoutPreferenceFile(p, false), "exact-bound ON output can change back to horizontal");
      Check(firefox_preferences::UpdateTabLayoutPreferenceFile(p, std::nullopt) && Read(p / L"user.js") == exact, "boundary release restores all original bytes");
      Write(p / L"user.js", full + ' ');
      Check(!firefox_preferences::UpdateTabLayoutPreferenceFile(p, std::nullopt) && Read(p / L"user.js") == full + ' ', "oversized input fails closed even during release");
    }
    {
      const auto p = profile(); fs::create_directory(p / L"user.js");
      Check(!firefox_preferences::UpdateTabLayoutPreferenceFile(p, true), "directory in place of user.js is rejected");
      Check(!firefox_preferences::UpdateTabLayoutPreferenceFile(p, std::nullopt), "Keep never treats an unsafe non-file as missing");
      Check(!firefox_preferences::UpdateTabLayoutPreferenceFile(p / L"missing", true), "missing profile directory is not created implicitly");
    }
    Check(Read(other / L"user.js") == otherBytes, "all refusal, locking and boundary cases leave the other client exact");
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if ((message.message >= WM_KEYFIRST && message.message <= WM_KEYLAST) ||
          (message.message >= WM_MOUSEFIRST && message.message <= WM_MOUSELAST)) throw std::runtime_error("Unexpected device input");
      DispatchMessageW(&message);
    }
    unsigned visible = 0;
    EnumWindows([](HWND w, LPARAM p) -> BOOL { DWORD pid = 0; GetWindowThreadProcessId(w, &pid); if (pid == GetCurrentProcessId() && IsWindowVisible(w)) ++*reinterpret_cast<unsigned *>(p); return TRUE; }, reinterpret_cast<LPARAM>(&visible));
    Check(visible == 0 && shows == 0 && focuses == 0 && foregrounds == 0, "native observer saw zero visible, focus or foreground events");
    for (auto hook : hooks) UnhookWinEvent(hook); hooks.clear();
    std::cout << "Firefox tab layout: " << checks << " checks, " << scenarios << " retained private scenarios, 0 failures; file-only, no browser or UI launch.\n";
    std::cout << "Firefox tab-layout guards: visible=" << visible << "; show=" << shows << "; focus=" << focuses << "; foreground=" << foregrounds << ".\n";
    std::wcout << L"Fixture retained: " << fixture.wstring() << L'\n';
    return 0;
  } catch (const std::exception &error) {
    for (auto hook : hooks) UnhookWinEvent(hook);
    std::cerr << "FAIL after " << checks << " checks: " << error.what() << "\nFixture retained: " << fixture.string() << '\n';
    return 1;
  }
}
