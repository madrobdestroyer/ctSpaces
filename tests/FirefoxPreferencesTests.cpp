#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../FirefoxPreferences.h"
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
static unsigned checks = 0;
static void Check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
static std::string Read(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), {}};
}
static void Write(const fs::path &path, const std::string &value) {
  std::ofstream output(path, std::ios::binary);
  output << value;
  if (!output) throw std::runtime_error("fixture write failed");
}
int wmain(int argc, wchar_t **argv) {
  fs::path fixture;
  try {
    if (argc != 1 && argc != 2) return 2;
    struct Case { const char *input; const char *expected; bool restore; };
    const Case cases[] = {
      {"// Example: user_pref(\"browser.startup.page\", 3);\r\nuser_pref(\"browser.startup.homepage\", \"https://example.test\");\r\n",
       "// Example: user_pref(\"browser.startup.page\", 3);\r\nuser_pref(\"browser.startup.homepage\", \"https://example.test\");\r\nuser_pref(\"browser.startup.page\", 3);\r\n", true},
      {"// Example: user_pref(\"browser.startup.page\", 1);\r\nuser_pref (\"browser.startup.page\", 3);\r\n",
       "// Example: user_pref(\"browser.startup.page\", 1);\r\nuser_pref (\"browser.startup.page\", 1);\r\n", false},
      {"/* user_pref(\"browser.startup.page\", 1);\n*/\nuser_pref /* note */ ( 'browser.startup.page' , -1 /* preserve */ );\n",
       "/* user_pref(\"browser.startup.page\", 1);\n*/\nuser_pref /* note */ ( 'browser.startup.page' , 3 /* preserve */ );\n", true},
      {"user_pref(\"browser.startup.page\", 1); user_pref('browser.startup.page', +2);\n",
       "user_pref(\"browser.startup.page\", 3); user_pref('browser.startup.page', 3);\n", true},
      {"user_pref(\"browser\\u002estartup\\x2epage\", 3);\n",
       "user_pref(\"browser\\u002estartup\\x2epage\", 1);\n", false},
      {"user_pref(\"example\", \"user_pref(\\\"browser.startup.page\\\", 1);\");\n",
       "user_pref(\"example\", \"user_pref(\\\"browser.startup.page\\\", 1);\");\nuser_pref(\"browser.startup.page\", 3);\n", true},
      {"// comment without a trailing newline",
       "// comment without a trailing newline\r\nuser_pref(\"browser.startup.page\", 1);\r\n", false},
      {"", "user_pref(\"browser.startup.page\", 3);\r\n", true},
      {"\xef\xbb\xbfuser_pref(\"browser.startup.page\", 1);\n",
       "\xef\xbb\xbfuser_pref(\"browser.startup.page\", 3);\n", true},
      {"user_pref\n(\n\"browser.startup.page\"\n,\n3\n)\n;\n",
       "user_pref\n(\n\"browser.startup.page\"\n,\n1\n)\n;\n", false},
      {"# Example: user_pref(\"browser.startup.page\", 1);\n",
       "# Example: user_pref(\"browser.startup.page\", 1);\nuser_pref(\"browser.startup.page\", 3);\n", true},
      {"  # Example: user_pref(\"browser.startup.page\", 3);\r\nuser_pref(\"browser.startup.page\", 3);\r\n",
       "  # Example: user_pref(\"browser.startup.page\", 3);\r\nuser_pref(\"browser.startup.page\", 1);\r\n", false},
      {"# Comment with no trailing newline: user_pref(\"browser.startup.page\", 1);",
       "# Comment with no trailing newline: user_pref(\"browser.startup.page\", 1);\r\nuser_pref(\"browser.startup.page\", 3);\r\n", true},
      {"user_pref\v(\"browser.startup.page\"\v,\v3\v);\n",
       "user_pref\v(\"browser.startup.page\"\v,\v1\v);\n", false},
      {"user_pref(\"description\", \"Useful multi\nline text\");\nuser_pref(\"browser.startup.page\", 1);\n",
       "user_pref(\"description\", \"Useful multi\nline text\");\nuser_pref(\"browser.startup.page\", 3);\n", true}
    };
    const fs::path fixtureBase = argc == 2 ? fs::absolute(fs::path(argv[1])) : fs::temp_directory_path();
    fs::create_directories(fixtureBase);
    fixture = fixtureBase / (L"ctSpaces-firefox-tests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    Check(fs::create_directory(fixture), "fresh fixture created");
    unsigned index = 0;
    for (const auto &test : cases) {
      std::string result;
      Check(firefox_preferences::UpdateStartupPreferenceText(test.input, test.restore, result), "valid preferences parsed");
      if (result != test.expected)
        std::cerr << "Syntax case " << index << "\nInput:\n" << test.input
                  << "\nActual:\n" << result << "\nExpected:\n" << test.expected << '\n';
      Check(result == test.expected, "comments/trivia/unrelated bytes preserved; actual calls updated");
      const fs::path profile = fixture / std::to_wstring(index++);
      fs::create_directory(profile);
      Write(profile / L"user.js", test.input);
      Check(firefox_preferences::UpdateStartupPreferenceFile(profile, test.restore), "production file update succeeds");
      Check(Read(profile / L"user.js") == test.expected, "production file contains expected active settings");
      Check(std::distance(fs::directory_iterator(profile), fs::directory_iterator{}) == 1, "no temporary file remains");
      Check(firefox_preferences::UpdateStartupPreferenceFile(profile, test.restore), "repeat update succeeds");
      Check(Read(profile / L"user.js") == test.expected, "update is idempotent");
    }
    for (const auto *invalid : {"/* unterminated comment", "user_pref(\"browser.startup.page\", name);", "user_pref(\"example\", \"unterminated", "user_pref(\"browser.startup.page\", 3.1);"}) {
      const fs::path profile = fixture / std::to_wstring(index++);
      fs::create_directory(profile);
      Write(profile / L"user.js", invalid);
      Check(!firefox_preferences::UpdateStartupPreferenceFile(profile, true), "unsafe parse fails");
      Check(Read(profile / L"user.js") == invalid, "unsafe input untouched");
    }
    const fs::path newProfile = fixture / L"new";
    fs::create_directory(newProfile);
    Check(firefox_preferences::UpdateStartupPreferenceFile(newProfile, true), "new profile gets active preference");
    const std::string before = Read(newProfile / L"user.js");
    HANDLE locked = CreateFileW((newProfile / L"user.js").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "destination locked fixture");
    Check(!firefox_preferences::UpdateStartupPreferenceFile(newProfile, false), "locked destination rejects atomic replace");
    CloseHandle(locked);
    Check(Read(newProfile / L"user.js") == before, "locked destination unchanged");
    Check(std::distance(fs::directory_iterator(newProfile), fs::directory_iterator{}) == 1, "failure staging cleaned");
    const fs::path oversized = fixture / L"oversized";
    fs::create_directory(oversized);
    Write(oversized / L"user.js", std::string(firefox_preferences::kMaximumUserJsBytes + 1, ' '));
    Check(!firefox_preferences::UpdateStartupPreferenceFile(oversized, true), "size limit enforced");
    // A first successful launch must never make its own next launch fail the
    // input limit. Exercise both append and replacement at that boundary.
    const fs::path fullWithoutKey = fixture / L"full-without-key";
    fs::create_directory(fullWithoutKey);
    const std::string atLimit(firefox_preferences::kMaximumUserJsBytes, ' ');
    Write(fullWithoutKey / L"user.js", atLimit);
    Check(!firefox_preferences::UpdateStartupPreferenceFile(fullWithoutKey, true),
          "a preference append exceeding the size bound is refused before publication");
    Check(Read(fullWithoutKey / L"user.js") == atLimit,
          "a refused size-boundary append preserves exact original preference bytes");
    Check(std::distance(fs::directory_iterator(fullWithoutKey), fs::directory_iterator{}) == 1,
          "a refused size-boundary append leaves no staging file");
    const fs::path roomForKey = fixture / L"room-for-key";
    fs::create_directory(roomForKey);
    const std::string startupLine = "user_pref(\"browser.startup.page\", 3);\r\n";
    const std::string room(firefox_preferences::kMaximumUserJsBytes - startupLine.size() - 2, ' ');
    Write(roomForKey / L"user.js", room);
    Check(firefox_preferences::UpdateStartupPreferenceFile(roomForKey, true),
          "an append fitting exactly within the size bound succeeds");
    const std::string fullManaged = room + "\r\n" + startupLine;
    Check(Read(roomForKey / L"user.js") == fullManaged && fullManaged.size() == firefox_preferences::kMaximumUserJsBytes,
          "the exact-boundary append preserves unrelated bytes and writes a valid managed preference");
    Check(firefox_preferences::UpdateStartupPreferenceFile(roomForKey, false),
          "the successful boundary output can be read and changed on the next launch");
    std::string disabled = fullManaged;
    disabled[disabled.size() - 5] = '1';
    Check(Read(roomForKey / L"user.js") == disabled,
          "a repeat boundary update changes only the requested startup value");
    Check(firefox_preferences::UpdateStartupPreferenceFile(roomForKey, false) && Read(roomForKey / L"user.js") == disabled,
          "repeated boundary launch updates remain idempotent");
    const fs::path directoryFile = fixture / L"directory-file";
    fs::create_directories(directoryFile / L"user.js");
    Check(!firefox_preferences::UpdateStartupPreferenceFile(directoryFile, true), "nonfile rejected");
    fs::remove_all(fixture);
    std::cout << checks << " Firefox preference checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL after " << checks << " checks: " << error.what() << "\nFixture retained: " << fixture.string() << "\n";
    return 1;
  }
}
