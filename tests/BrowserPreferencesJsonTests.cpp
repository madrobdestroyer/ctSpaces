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

class TemporaryDirectory {
public:
  TemporaryDirectory() {
    const fs::path tempRoot = fs::temp_directory_path();
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
      const fs::path tempRoot = fs::temp_directory_path().lexically_normal();
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

} // namespace

int main() {
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
  std::cout << "BrowserPreferencesJson tests passed\n";
  return 0;
}
