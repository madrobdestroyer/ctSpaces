#include "../BrowserPreferencesJson.h"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#pragma comment(lib, "Ole32.lib")

namespace fs = std::filesystem;
using browser_preferences::EditStartupPreferencesJson;
using browser_preferences::FileUpdateResult;
using browser_preferences::JsonEditResult;
using browser_preferences::UpdateStartupPreferencesFile;
using browser_preferences::kMaximumPreferencesJsonBytes;

namespace {

int failures = 0;
unsigned checks = 0;
fs::path fixtureBase;

class TemporaryDirectory {
public:
  TemporaryDirectory() {
    const fs::path tempRoot = fixtureBase.empty() ? fs::temp_directory_path() : fixtureBase;
    fs::create_directories(tempRoot);
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
      GUID guid{};
      if (FAILED(CoCreateGuid(&guid)))
        throw std::runtime_error("CoCreateGuid failed");

      wchar_t guidText[40]{};
      if (StringFromGUID2(guid, guidText,
                          static_cast<int>(std::size(guidText))) == 0) {
        throw std::runtime_error("StringFromGUID2 failed");
      }
      path_ = tempRoot / (L"ctspaces-json-test-" + std::wstring(guidText));
      if (CreateDirectoryW(path_.c_str(), nullptr))
        return;
      if (GetLastError() != ERROR_ALREADY_EXISTS)
        break;
    }
    path_.clear();
    throw std::runtime_error("Could not create an exclusive test directory");
  }

  ~TemporaryDirectory() {
    if (path_.empty())
      return;
    try {
      const fs::path tempRoot = (fixtureBase.empty() ? fs::temp_directory_path() : fixtureBase).lexically_normal();
      const fs::path candidate = path_.lexically_normal();
      const std::wstring name = candidate.filename().wstring();
      const DWORD attributes = GetFileAttributesW(candidate.c_str());
      if (candidate.parent_path() != tempRoot ||
          name.rfind(L"ctspaces-json-test-{", 0) != 0 ||
          attributes == INVALID_FILE_ATTRIBUTES ||
          (attributes & (FILE_ATTRIBUTE_DIRECTORY |
                         FILE_ATTRIBUTE_REPARSE_POINT)) !=
              FILE_ATTRIBUTE_DIRECTORY) {
        return;
      }
      std::error_code cleanupError;
      fs::remove_all(candidate, cleanupError);
    } catch (...) {
    }
  }

  const fs::path &path() const { return path_; }

private:
  fs::path path_;
};

void Expect(bool condition, const char *message) {
  ++checks;
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void ExpectEdit(std::string_view input, bool restoreTabs, bool ensure,
                JsonEditResult expectedResult, std::string_view expected,
                const char *message) {
  std::string output;
  const JsonEditResult result = EditStartupPreferencesJson(
      input, restoreTabs, ensure, output);
  Expect(result == expectedResult, message);
  Expect(output == expected, message);
}

std::string ReadFile(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void TestBoundedStartupStageNames(const fs::path &directory) {
  const std::string original =
      "{\"session\":{\"restore_on_startup\":5},\"keep\":\"external bytes\"}";
  const std::string expected =
      "{\"session\":{\"restore_on_startup\":1},\"keep\":\"external bytes\"}";
  for (const size_t length : {size_t{249}, size_t{259}}) {
    const fs::path parent = directory /
        std::wstring(length - directory.native().size() - 13, L'x');
    fs::create_directory(parent);
    const fs::path preferences = parent / L"Preferences";
    {
      std::ofstream output(preferences, std::ios::binary);
      output << original;
      if (!output) throw std::runtime_error("Could not write long-path preferences fixture");
    }
    std::wstring details;
    const auto result = UpdateStartupPreferencesFile(preferences, true, true, &details);
    std::cout << "Startup path length=" << preferences.native().size()
              << "; update result=" << static_cast<unsigned>(result) << '\n';
    if (!details.empty()) std::wcout << L"Startup update details: " << details << L'\n';
    Expect(result == FileUpdateResult::updated,
           "valid near-MAX_PATH Preferences supports atomic staging");
    Expect(ReadFile(preferences) == expected,
           "long-path startup update preserves unrelated bytes");
    Expect(std::distance(fs::directory_iterator(parent), fs::directory_iterator{}) == 1,
           "long-path startup update leaves no staging sibling");
  }
  const fs::path shortDirectory = directory / L"tiny-staging";
  fs::create_directory(shortDirectory);
  const fs::path tiny = shortDirectory / L"x";
  {
    std::ofstream output(tiny, std::ios::binary);
    output << original;
    if (!output) throw std::runtime_error("Could not write tiny preferences fixture");
  }
  for (unsigned repeat = 0; repeat < 80; ++repeat) {
    Expect(UpdateStartupPreferencesFile(tiny, repeat % 2 == 0, true) ==
               FileUpdateResult::updated,
           "repeated bounded startup staging commits the requested setting");
    const std::string wanted = repeat % 2 == 0 ? expected : original;
    Expect(ReadFile(tiny) == wanted,
           "repeated tiny-name startup updates preserve unrelated bytes");
    Expect(std::distance(fs::directory_iterator(shortDirectory), fs::directory_iterator{}) == 1,
           "repeated startup updates leave no staging siblings");
  }
  const std::wstring_view alphabet = L"0123456789abcdefghijklmnopqrstuvwxyz_-~";
  for (wchar_t character : alphabet) if (character != L'x') {
    std::ofstream output(shortDirectory / std::wstring(1, character), std::ios::binary);
    output << "COLLISION SENTINEL";
    if (!output) throw std::runtime_error("Could not write staging collision fixture");
  }
  const auto count = std::distance(fs::directory_iterator(shortDirectory), fs::directory_iterator{});
  Expect(UpdateStartupPreferencesFile(tiny, true, true) == FileUpdateResult::io_error,
         "exhausted tiny staging namespace fails closed");
  Expect(ReadFile(tiny) == original,
         "stage namespace exhaustion preserves the existing preferences");
  Expect(std::distance(fs::directory_iterator(shortDirectory), fs::directory_iterator{}) == count,
         "stage namespace exhaustion leaves every occupied file in place");
  for (wchar_t character : alphabet) if (character != L'x')
    Expect(ReadFile(shortDirectory / std::wstring(1, character)) == "COLLISION SENTINEL",
           "failed staging never changes a collision sentinel");
  std::cout << "Bounded startup staging: 249/259-character paths, 80 alternating updates, exhausted tiny-name namespace\n";
}

void TestStartupParserAdversarialCases() {
  const std::string unrelated =
      "\"extensions\":{\"settings\":{\"session\":{\"restore_on_startup\":77,"
      "\"startup_urls\":[\"[ ] } \\\" \\\\ /\"],\"profile\":{\"exit_type\":\"Crashed\"}}}}";
  unsigned scenarios = 0;
  for (unsigned id = 0; id < 200; ++id) {
    const std::string trivia = id % 2 ? " \r\n\t" : "";
    const std::string marker = "\"opaque\":\"" + std::to_string(id) + "-\\u65e5\\ud83d\\ude80\",";
    const std::string sessionKey = id % 3 ? "\"session\"" : "\"sess\\u0069on\"";
    const std::string input = "{" + trivia + marker + unrelated + "," + sessionKey +
        ":{" + trivia + "\"restore_on_startup\":5,\"startup_urls\":[\"https://example.test/a?b=]c\"],"
        "\"urls_to_restore_on_startup\":[\"keep-original\"]},\"profile\":{\"exit_type\":\"Crashed\",\"exited_cleanly\":false}}";
    for (const bool restore : {false, true}) {
      const std::string wanted = "{" + trivia + marker + unrelated + "," + sessionKey +
          ":{" + trivia + "\"restore_on_startup\":" + (restore ? "1" : "5") +
          ",\"startup_urls\":" + (restore ? "[\"https://example.test/a?b=]c\"]" : "[]") +
          ",\"urls_to_restore_on_startup\":" + (restore ? "[\"keep-original\"]" : "[]") +
          "},\"profile\":{\"exit_type\":\"Normal\",\"exited_cleanly\":true}}";
      ExpectEdit(input, restore, true, JsonEditResult::updated, wanted,
                 "generated startup documents preserve nested names, Unicode escapes, strings and trivia");
      ExpectEdit(wanted, restore, true, JsonEditResult::unchanged, wanted,
                 "generated startup edits are idempotent");
      ++scenarios;
    }
    for (const size_t cut : {size_t{0}, size_t{1}, input.size() / 2, input.size() - 1}) {
      const auto truncated = input.substr(0, cut);
      ExpectEdit(truncated, true, true, JsonEditResult::invalid_json, truncated,
                 "truncated generated documents fail without changing bytes");
    }
  }
  const std::string malformed[] = {
    "{\"keep\":\"\\ud800\"}", "{\"keep\":\"\\udc00\"}",
    std::string("{\"keep\":\"") + '\xc0' + '\xaf' + "\"}",
    std::string("{\"keep\":\"") + '\xed' + '\xa0' + '\x80' + "\"}",
    "{\"keep\":01}", "{\"keep\":1.}", "{\"keep\":1e+}",
    "{\"keep\":[true false]}", "{\"keep\":NaN}", "{\"keep\":Infinity}"
  };
  for (const auto &input : malformed)
    ExpectEdit(input, false, true, JsonEditResult::invalid_json, input,
               "malformed Unicode/numbers/delimiters fail without edits");
  const std::string tooDeep = "{\"keep\":" + std::string(257, '[') + "0" + std::string(257, ']') + "}";
  ExpectEdit(tooDeep, false, true, JsonEditResult::invalid_json, tooDeep,
             "excessive JSON nesting fails without stack or byte damage");
  std::cout << "Generated startup parser scenarios=" << scenarios
            << "; truncation cases=800; malformed Unicode/numeric/depth cases=11\n";
}

void TestStartupOutputSizeBoundary(const fs::path &directory) {
  const fs::path preferences = directory / L"SizeBoundaryPreferences";
  for (const std::string originalRoot : {std::string("{}"), std::string("{\"session\":{}}")}) {
    const std::string input = originalRoot + std::string(kMaximumPreferencesJsonBytes - originalRoot.size(), ' ');
    std::string output;
    Expect(EditStartupPreferencesJson(input, true, true, output) == JsonEditResult::unsafe_target,
           "an inserted restore setting cannot grow JSON beyond the next launch's readable limit");
    Expect(output == input,
           "a refused output-size edit returns exact original preferences");
    {
      std::ofstream file(preferences, std::ios::binary | std::ios::trunc);
      file << input;
      if (!file) throw std::runtime_error("Could not seed exact-limit startup preferences");
    }
    Expect(UpdateStartupPreferencesFile(preferences, true, true) == FileUpdateResult::unsafe_target,
           "an oversized transformed file is refused before publication");
    Expect(ReadFile(preferences) == input,
           "refused oversized startup publication preserves every original byte");
    Expect(std::distance(fs::directory_iterator(directory), fs::directory_iterator{}) == 1,
           "refused output-size publication creates no staging sibling");
  }
  const std::string original = "{\"session\":{\"restore_on_startup\":5},\"keep\":true}";
  const std::string input = original + std::string(kMaximumPreferencesJsonBytes - original.size(), ' ');
  {
    std::ofstream file(preferences, std::ios::binary | std::ios::trunc);
    file << input;
    if (!file) throw std::runtime_error("Could not seed exact-limit replacement preferences");
  }
  std::string expected = input;
  expected[expected.find(":5") + 1] = '1';
  Expect(UpdateStartupPreferencesFile(preferences, true, true) == FileUpdateResult::updated,
         "a same-size startup replacement at the exact limit remains supported");
  Expect(ReadFile(preferences) == expected,
         "exact-limit startup replacement preserves unrelated bytes");
  Expect(UpdateStartupPreferencesFile(preferences, false, true) == FileUpdateResult::updated && ReadFile(preferences) == input,
         "an exact-limit successful update can be read and changed on the next launch");
  Expect(UpdateStartupPreferencesFile(preferences, false, true) == FileUpdateResult::unchanged,
         "repeating a successful exact-limit setting remains idempotent");
  std::cout << "Startup output size boundary: 14 checks; missing root/session settings and repeated exact-limit updates\n";
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  if (argc == 2) fixtureBase = fs::absolute(fs::path(argv[1]));
  else if (argc != 1) return 2;
  TestStartupParserAdversarialCases();
  const std::string nestedInput =
      "{\n"
      "  \"session\" : { \"restore_on_startup\" : 1, "
      "\"startup_urls\" : [\"https://example.test/a]b\"], "
      "\"urls_to_restore_on_startup\":[\"keep? no\"], "
      "\"nested\":{\"restore_on_startup\":77}},\n"
      "  \"profile\":{\"exit_type\":\"Crashed\",\"exited_cleanly\":false," 
      "\"nested\":{\"exit_type\":\"Nested\",\"exited_cleanly\":false}},\n"
      "  \"extensions\":{\"settings\":{\"id\":{" 
      "\"restore_on_startup\":88,\"startup_urls\":[\"untouched\"]," 
      "\"urls_to_restore_on_startup\":[\"untouched\"],"
      "\"exit_type\":\"Untouched\",\"exited_cleanly\":false}}}\n"
      "}\n";
  const std::string nestedExpected =
      "{\n"
      "  \"session\" : { \"restore_on_startup\" : 5, "
      "\"startup_urls\" : [], "
      "\"urls_to_restore_on_startup\":[], "
      "\"nested\":{\"restore_on_startup\":77}},\n"
      "  \"profile\":{\"exit_type\":\"Normal\",\"exited_cleanly\":true," 
      "\"nested\":{\"exit_type\":\"Nested\",\"exited_cleanly\":false}},\n"
      "  \"extensions\":{\"settings\":{\"id\":{" 
      "\"restore_on_startup\":88,\"startup_urls\":[\"untouched\"]," 
      "\"urls_to_restore_on_startup\":[\"untouched\"],"
      "\"exit_type\":\"Untouched\",\"exited_cleanly\":false}}}\n"
      "}\n";
  ExpectEdit(nestedInput, false, true, JsonEditResult::updated,
             nestedExpected, "only exact direct paths are edited");

  ExpectEdit(
      "{\"session\":{\"restore_on_startup\":5,\"startup_urls\":[\"x\"]},"
      "\"profile\":{\"exit_type\":\"Normal\",\"exited_cleanly\":true}}",
      true, true, JsonEditResult::updated,
      "{\"session\":{\"restore_on_startup\":1,\"startup_urls\":[\"x\"]},"
      "\"profile\":{\"exit_type\":\"Normal\",\"exited_cleanly\":true}}",
      "restore mode preserves startup URLs");

  ExpectEdit("{\"session\":{\"other\":1},\"keep\":2}", true, true,
             JsonEditResult::updated,
             "{\"session\":{\"restore_on_startup\":1,\"other\":1},"
             "\"keep\":2}",
             "missing restore preference is minimally inserted");
  ExpectEdit("{ \"keep\" : [1, 2] }", false, true,
             JsonEditResult::updated,
             "{\"session\":{\"restore_on_startup\":5}, \"keep\" : [1, 2] }",
             "missing session object is minimally inserted");
  ExpectEdit("{}", true, false, JsonEditResult::unchanged, "{}",
             "local-state style edit does not create session data");

  ExpectEdit("{\"session\":{},\"session\":{}}", true, true,
             JsonEditResult::unsafe_target,
             "{\"session\":{},\"session\":{}}",
             "duplicate intermediate target fails closed");
  ExpectEdit("{\"session\":{\"restore_on_startup\":1,"
             "\"restore_on_startup\":5}}",
             true, true, JsonEditResult::unsafe_target,
             "{\"session\":{\"restore_on_startup\":1,"
             "\"restore_on_startup\":5}}",
             "duplicate leaf target fails closed");
  ExpectEdit("{\"session\":42}", true, true,
             JsonEditResult::unsafe_target, "{\"session\":42}",
             "non-object intermediate target fails closed");
  ExpectEdit("{\"session\":{\"restore_on_startup\":1,}}", true, true,
             JsonEditResult::invalid_json,
             "{\"session\":{\"restore_on_startup\":1,}}",
             "malformed JSON is not changed");
  ExpectEdit("{\"sess\\u0069on\":{\"restore_on_startup\":5}}", true,
             true, JsonEditResult::updated,
             "{\"sess\\u0069on\":{\"restore_on_startup\":1}}",
             "escaped path keys are matched semantically");

  const std::string oversized(kMaximumPreferencesJsonBytes + 1, ' ');
  ExpectEdit(oversized, true, true, JsonEditResult::unsafe_target, oversized,
             "oversized JSON is rejected before scanning");

  TemporaryDirectory temporary;
  const fs::path tempDirectory = temporary.path();
  const fs::path sizeBoundary = tempDirectory / L"size-boundary";
  fs::create_directory(sizeBoundary);
  TestStartupOutputSizeBoundary(sizeBoundary);
  TestBoundedStartupStageNames(tempDirectory);
  const fs::path preferences = tempDirectory / L"Preferences";
  {
    std::ofstream output(preferences, std::ios::binary);
    output << "{\"session\":{\"restore_on_startup\":5},\"keep\":7}";
  }
  Expect(UpdateStartupPreferencesFile(preferences, true, true) ==
             FileUpdateResult::updated,
         "atomic file edit reports updated");
  Expect(ReadFile(preferences) ==
             "{\"session\":{\"restore_on_startup\":1},\"keep\":7}",
         "atomic file edit installs validated output");

  {
    std::ofstream output(preferences, std::ios::binary | std::ios::trunc);
    output << "{broken";
  }
  Expect(UpdateStartupPreferencesFile(preferences, false, true) ==
             FileUpdateResult::invalid_json,
         "malformed file reports invalid JSON");
  Expect(ReadFile(preferences) == "{broken",
         "malformed file remains byte-for-byte unchanged");

  const fs::path reparsePoint = tempDirectory / L"Preferences-link";
  constexpr DWORD allowUnprivilegedSymlinkCreation = 0x2;
  if (CreateSymbolicLinkW(reparsePoint.c_str(), preferences.c_str(),
                          allowUnprivilegedSymlinkCreation)) {
    Expect(UpdateStartupPreferencesFile(reparsePoint, false, true) ==
               FileUpdateResult::unsafe_target,
           "reparse-point file target fails closed");
    Expect(ReadFile(preferences) == "{broken",
           "reparse-point rejection leaves its destination unchanged");
    DeleteFileW(reparsePoint.c_str());
  }
  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }
  std::cout << checks << " BrowserPreferencesJson checks passed\n";
  return 0;
}
