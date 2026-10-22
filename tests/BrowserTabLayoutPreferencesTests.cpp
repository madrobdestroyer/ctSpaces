// File-only production adapters. Every Preferences document is synthetic and
// retained below the requested fixture root; no real browser/profile is opened.
#define NOMINMAX
#include <windows.h>
#include "RetainedFixture.h"
#include "../BrowserPreferencesJson.h"
#include "../ConfigPersistence.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace browser_preferences;
static unsigned checks = 0, failures = 0, scenarios = 0;
static unsigned shows = 0, focuses = 0, foregrounds = 0;
static void Check(bool value, const char *label) {
  ++checks; if (!value) ++failures;
  std::cout << (value ? "PASS " : "FAIL ") << label << '\n';
}
static std::string Read(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Owned fixture read failed");
  return {std::istreambuf_iterator<char>(input), {}};
}
static bool Write(const fs::path &path, const std::string &bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(output);
}
static std::string Document(bool edge, bool chrome, bool brave) {
  const auto boolean = [](bool v) { return v ? "true" : "false"; };
  return std::string("\xef\xbb\xbf{\r\n  \"edge\":{\"vertical_tabs\":{\"opened\":") +
      boolean(edge) + ",\"unrelated\":17}},\r\n  \"vertical_tabs\":{\"enabled\":" +
      boolean(chrome) + ",\"collapsed_state\":true},\r\n  \"brave\":{\"tabs\":{\"vertical_tabs_enabled\":" +
      boolean(brave) + ",\"vertical_tabs_expanded_width\":307}},\r\n  "
      "\"session\":{\"restore_on_startup\":1,\"startup_urls\":[\"https://example.test/?q=]\\\"\"]},"
      "\"profile\":{\"exit_type\":\"Crashed\",\"exited_cleanly\":false},"
      "\"extensions\":{\"vertical_tabs\":{\"enabled\":true}},"
      "\"opaque\":\"\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e \\ud83d\\ude80\","
      "\"unchanged-number\":1.20e+03}\r\n";
}
static std::string PathObject(TabLayoutBrowser browser, const std::string &value) {
  switch (browser) {
  case TabLayoutBrowser::Edge: return "{\"edge\":{\"vertical_tabs\":{\"opened\":" + value + "}}}";
  case TabLayoutBrowser::Chrome: return "{\"vertical_tabs\":{\"enabled\":" + value + "}}";
  case TabLayoutBrowser::Brave: return "{\"brave\":{\"tabs\":{\"vertical_tabs_enabled\":" + value + "}}}";
  }
  throw std::runtime_error("Unknown fixture browser");
}
static void Expect(const std::string &input, TabLayoutBrowser browser, bool vertical,
                   JsonEditResult status, const std::string &expected, const char *label) {
  std::string output = "OLD OUTPUT MUST BE REPLACED";
  Check(EditTabLayoutPreferencesJson(input, browser, vertical, output) == status &&
        output == expected, label);
}
static void PureCases() {
  for (auto browser : {TabLayoutBrowser::Edge, TabLayoutBrowser::Chrome, TabLayoutBrowser::Brave}) {
    ++scenarios;
    const auto on = Document(browser == TabLayoutBrowser::Edge,
                             browser == TabLayoutBrowser::Chrome,
                             browser == TabLayoutBrowser::Brave);
    Expect(Document(false, false, false), browser, true, JsonEditResult::updated, on,
           "Vertical mode changes only the selected browser boolean and preserves all other exact bytes");
    Expect(on, browser, true, JsonEditResult::unchanged, on,
           "Repeated vertical mode is byte-exact and idempotent");
    Expect(on, browser, false, JsonEditResult::updated, Document(false, false, false),
           "Horizontal mode restores only the selected browser leaf");
    Expect(Document(false, false, false), browser, false, JsonEditResult::unchanged,
           Document(false, false, false), "Repeated horizontal mode is unchanged");
    Expect("{}", browser, true, JsonEditResult::updated, PathObject(browser, "true"),
           "Missing native path is minimally inserted into an empty document");
    const auto inserted = PathObject(browser, "false");
    Expect("{}", browser, false, JsonEditResult::updated, inserted,
           "Explicit horizontal mode inserts a real false preference rather than omitting authority");
    std::string alias = on;
    Check(EditTabLayoutPreferencesJson(alias, browser, false, alias) == JsonEditResult::updated &&
          alias == Document(false, false, false), "Input and output may safely refer to the same owned string");
    for (const auto &value : {"null", "0", "1", "\"true\"", "[]", "{}"}) {
      const auto input = PathObject(browser, value);
      Expect(input, browser, true, JsonEditResult::unsafe_target, input,
             "Existing non-boolean native layout values fail closed without coercion");
    }
  }
  Expect("{\"brave\": { \"keep\":7 }}", TabLayoutBrowser::Brave, true, JsonEditResult::updated,
         "{\"brave\": {\"tabs\":{\"vertical_tabs_enabled\":true}, \"keep\":7 }}",
         "Insertion preserves an existing nonempty Brave ancestor and its whitespace");
  Expect("{\"edge\":{\"vertical_tabs\": { \n}}}", TabLayoutBrowser::Edge, true, JsonEditResult::updated,
         "{\"edge\":{\"vertical_tabs\": {\"opened\":true \n}}}",
         "Insertion into an empty native object retains its original inner whitespace");
  Expect("{\"vertical_tabs\":{\"keep\":1}}", TabLayoutBrowser::Chrome, false, JsonEditResult::updated,
         "{\"vertical_tabs\":{\"enabled\":false,\"keep\":1}}",
         "Missing Chrome leaf is inserted without replacing existing collapse or other options");
}

static void TargetAndSyntaxCases() {
  struct Case { TabLayoutBrowser browser; std::string text; };
  const std::vector<Case> unsafe{
    {TabLayoutBrowser::Edge, "{\"edge\":{},\"edg\\u0065\":{}}"},
    {TabLayoutBrowser::Edge, "{\"edge\":{\"vertical_tabs\":{},\"vertical_\\u0074abs\":{}}}"},
    {TabLayoutBrowser::Edge, "{\"edge\":{\"vertical_tabs\":{\"opened\":true,\"op\\u0065ned\":false}}}"},
    {TabLayoutBrowser::Chrome, "{\"vertical_tabs\":{},\"vertical_\\u0074abs\":{}}"},
    {TabLayoutBrowser::Chrome, "{\"vertical_tabs\":{\"enabled\":true,\"en\\u0061bled\":false}}"},
    {TabLayoutBrowser::Brave, "{\"brave\":{},\"brav\\u0065\":{}}"},
    {TabLayoutBrowser::Brave, "{\"brave\":{\"tabs\":{},\"ta\\u0062s\":{}}}"},
    {TabLayoutBrowser::Brave, "{\"brave\":{\"tabs\":{\"vertical_tabs_enabled\":true,\"vertical_tabs_enabl\\u0065d\":false}}}"},
    {TabLayoutBrowser::Edge, "{\"edge\":[]}"},
    {TabLayoutBrowser::Edge, "{\"edge\":{\"vertical_tabs\":null}}"},
    {TabLayoutBrowser::Chrome, "{\"vertical_tabs\":false}"},
    {TabLayoutBrowser::Brave, "{\"brave\":42}"},
    {TabLayoutBrowser::Brave, "{\"brave\":{\"tabs\":\"not an object\"}}"},
  };
  ++scenarios;
  for (const auto &test : unsafe)
    Expect(test.text, test.browser, true, JsonEditResult::unsafe_target, test.text,
           "Duplicate semantic target chains and non-object ancestors preserve original bytes");
  const std::string escaped = "{\"vert\\u0069cal_tabs\":{\"enab\\u006ced\":false},\"keep\":1}";
  Expect(escaped, TabLayoutBrowser::Chrome, true, JsonEditResult::updated,
         "{\"vert\\u0069cal_tabs\":{\"enab\\u006ced\":true},\"keep\":1}",
         "Escaped target keys are recognized without rewriting their spelling");
  for (const auto &input : std::vector<std::string>{"", "[]", "null", "{\"keep\":01}",
      "{\"vertical_tabs\":{\"enabled\":false,}}", "{\"keep\":\"\\ud800\"}",
      std::string("{\"keep\":\"") + '\xc0' + '\xaf' + "\"}",
      "{\"keep\":" + std::string(257, '[') + "0" + std::string(257, ']') + "}"})
    Expect(input, TabLayoutBrowser::Chrome, true, JsonEditResult::invalid_json, input,
           "Malformed JSON, invalid Unicode and excessive depth are rejected unchanged");
  Expect("{\"session\":null,\"profile\":42,\"keep\":1,\"keep\":2}", TabLayoutBrowser::Chrome,
         true, JsonEditResult::updated,
         "{\"vertical_tabs\":{\"enabled\":true},\"session\":null,\"profile\":42,\"keep\":1,\"keep\":2}",
         "Unrelated startup shapes and duplicate non-target keys are preserved, not reinterpreted");
  Expect("{}", static_cast<TabLayoutBrowser>(99), true, JsonEditResult::unsafe_target, "{}",
         "An unknown browser enum cannot write a guessed path");
}

static void BoundCases(const fs::path &root) {
  ++scenarios;
  const auto path = root / L"bounded-Preferences";
  const std::string missing = "{}" + std::string(kMaximumPreferencesJsonBytes - 2, ' ');
  Expect(missing, TabLayoutBrowser::Chrome, true, JsonEditResult::unsafe_target, missing,
         "A missing path cannot grow a maximum-size document beyond the readable limit");
  Check(Write(path, missing), "Seed exact-limit owned document");
  Check(UpdateTabLayoutPreferencesFile(path, TabLayoutBrowser::Chrome, true) == FileUpdateResult::unsafe_target &&
        Read(path) == missing, "Oversized transformed output never reaches publication");
  const auto prefix = PathObject(TabLayoutBrowser::Chrome, "false");
  const std::string input = prefix + std::string(kMaximumPreferencesJsonBytes - prefix.size(), ' ');
  const auto wantedPrefix = PathObject(TabLayoutBrowser::Chrome, "true");
  const auto wanted = wantedPrefix + std::string(kMaximumPreferencesJsonBytes - prefix.size(), ' ');
  Check(Write(path, input) && UpdateTabLayoutPreferencesFile(path, TabLayoutBrowser::Chrome, true) == FileUpdateResult::updated &&
        Read(path) == wanted, "A bounded boolean replacement at the exact input limit remains usable");
  Check(UpdateTabLayoutPreferencesFile(path, TabLayoutBrowser::Chrome, false) == FileUpdateResult::updated &&
        Read(path) == input, "The maximum-size original can be recovered through the inverse native preference");
  const std::string tooLarge(kMaximumPreferencesJsonBytes + 1, ' ');
  Expect(tooLarge, TabLayoutBrowser::Chrome, false, JsonEditResult::unsafe_target, tooLarge,
         "Oversized input is refused before scanning");
}
using Boundary = config_persistence::IniPublicationBoundary;
struct Fault { unsigned mode = 0; bool reached = false, changed = false; std::string newer; };
static void Inject(const fs::path &live, const fs::path &auxiliary, Boundary stage, void *opaque) {
  auto &fault = *static_cast<Fault *>(opaque);
  const auto replace = [&](const fs::path &target) {
    const auto source = live.parent_path() / L"competing-source";
    return Write(source, fault.newer) && ReplaceFileW(target.c_str(), source.c_str(), nullptr,
        REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr) != FALSE;
  };
  if (stage == Boundary::AfterStageCopy && fault.mode == 1) {
    fault.reached = true; fault.changed = Write(live, fault.newer);
  } else if (stage == Boundary::AfterStageCopy && fault.mode == 2) {
    fault.reached = true; fault.changed = replace(auxiliary);
  } else if (stage == Boundary::BeforePublish && fault.mode == 3) {
    fault.reached = true; fault.changed = replace(live);
  } else if (stage == Boundary::BeforeNativePublication && fault.mode == 4) {
    fault.reached = true; fault.changed = replace(auxiliary);
  } else if (stage == Boundary::AfterPublish && fault.mode == 5) {
    fault.reached = true; fault.changed = replace(live);
  }
}
struct ResetHook { ~ResetHook() { config_persistence::SetIniPublicationHookForTest(nullptr, nullptr); } };
static bool HasBytes(const fs::path &directory, const std::string &wanted) {
  for (const auto &entry : fs::recursive_directory_iterator(directory))
    if (entry.is_regular_file() && Read(entry.path()) == wanted) return true;
  return false;
}
static void FileCases(const fs::path &root) {
  for (auto browser : {TabLayoutBrowser::Edge, TabLayoutBrowser::Chrome, TabLayoutBrowser::Brave}) {
    ++scenarios;
    const auto directory = root / std::to_wstring(static_cast<unsigned>(browser));
    fs::create_directory(directory); const auto path = directory / L"Preferences";
    const auto original = Document(false, false, false);
    const auto expected = Document(browser == TabLayoutBrowser::Edge,
        browser == TabLayoutBrowser::Chrome, browser == TabLayoutBrowser::Brave);
    Check(UpdateTabLayoutPreferencesFile(path, browser, true) == FileUpdateResult::missing && !fs::exists(path),
          "Missing profile Preferences stays missing and is never fabricated");
    Check(Write(path, original), "Seed independent owned browser document");
    std::wstring details;
    Check(UpdateTabLayoutPreferencesFile(path, browser, true, &details) == FileUpdateResult::updated &&
          Read(path) == expected && details.empty(), "Actual native publication changes only the selected browser's raw boolean bytes");
    Check(UpdateTabLayoutPreferencesFile(path, browser, true) == FileUpdateResult::unchanged,
          "Native repeated preference publication is idempotent");
    HANDLE held = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (held == INVALID_HANDLE_VALUE) throw std::runtime_error("Owned publication lock unavailable");
    const auto locked = UpdateTabLayoutPreferencesFile(path, browser, false, &details); CloseHandle(held);
    Check(locked == FileUpdateResult::io_error && Read(path) == expected,
          "Actual native sharing denial retains every original preference byte");
    Check(UpdateTabLayoutPreferencesFile(path, browser, false) == FileUpdateResult::updated && Read(path) == original,
          "Unlocked native retry restores horizontal layout with all other browsers intact");
    Check(std::distance(fs::directory_iterator(directory), fs::directory_iterator{}) == 1,
          "Successful and refused ordinary changes leave no unrelated staging files");
    Check(Write(path, PathObject(browser, "\"wrong type\"")), "Seed owned invalid-type file");
    Check(UpdateTabLayoutPreferencesFile(path, browser, true) == FileUpdateResult::unsafe_target &&
          Read(path) == PathObject(browser, "\"wrong type\""), "Native file adapter refuses unsafe leaf types before any write");
  }
  for (unsigned mode = 1; mode <= 7; ++mode) {
    ++scenarios; const auto directory = root / (L"publication-" + std::to_wstring(mode));
    fs::create_directory(directory); const auto path = directory / L"Preferences";
    const auto original = Document(false, false, false);
    if (!Write(path, original)) throw std::runtime_error("Native fault fixture unavailable");
    Fault fault{mode, false, false, "{\"newer\":\"INDEPENDENT WRITER\"}"};
    config_persistence::SetIniPublicationHookForTest(Inject, &fault); ResetHook reset;
    if (mode >= 6) config_persistence::FailIniPublicationForTest(mode == 6 ?
        config_persistence::IniPublicationFailure::OldDisplaced : config_persistence::IniPublicationFailure::Installed);
    std::wstring details; const auto result = UpdateTabLayoutPreferencesFile(path, TabLayoutBrowser::Brave, true, &details);
    Check(result == FileUpdateResult::io_error && !details.empty(),
          "A changed or partially published native transaction reports failure with diagnostics");
    if (mode <= 5) {
      Check(fault.reached && fault.changed, "The concurrent fixture reaches and changes the actual native publication boundary");
      Check(HasBytes(directory, fault.newer), "A rejected update preserves the independent newer writer byte-exact");
      if (mode == 1 || mode == 3) Check(Read(path) == fault.newer, "A newer source writer remains or is conditionally restored as live preferences");
      if (mode == 2) Check(Read(path) == original, "A substituted private stage cannot alter original preferences");
      if (mode >= 4) Check(HasBytes(directory, original), "Late native substitution retains the exact original in live or recovery data");
    } else Check(Read(path) == original, "Both partial native-publication failure classes restore exact original preferences");
  }
}
static void CALLBACK Observe(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG, DWORD, DWORD) {
  DWORD pid = 0; if (!window || !GetWindowThreadProcessId(window, &pid) || pid != GetCurrentProcessId()) return;
  if (event == EVENT_OBJECT_SHOW && object == OBJID_WINDOW) ++shows;
  if (event == EVENT_OBJECT_FOCUS) ++focuses;
  if (event == EVENT_SYSTEM_FOREGROUND) ++foregrounds;
}
int wmain(int argc, wchar_t **argv) {
  std::vector<HWINEVENTHOOK> hooks;
  try {
    if (argc != 2) return 2; std::cout << std::unitbuf;
    for (DWORD event : {EVENT_OBJECT_SHOW, EVENT_OBJECT_FOCUS, EVENT_SYSTEM_FOREGROUND}) {
      auto hook = SetWinEventHook(event, event, nullptr, Observe, GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
      if (!hook) throw std::runtime_error("Owned native event observer unavailable"); hooks.push_back(hook);
    }
    const auto root = retained_fixture::CreateRoot(fs::absolute(fs::path(argv[1])), L"tab-layout");
    PureCases(); TargetAndSyntaxCases(); BoundCases(root); FileCases(root);
    MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
    unsigned windows = 0; EnumWindows([](HWND window, LPARAM count) -> BOOL {
      DWORD pid = 0; GetWindowThreadProcessId(window, &pid); if (pid == GetCurrentProcessId()) ++*reinterpret_cast<unsigned *>(count); return TRUE;
    }, reinterpret_cast<LPARAM>(&windows));
    Check(windows == 0 && shows == 0 && focuses == 0 && foregrounds == 0,
          "Native event audit: no owned windows, show, focus or foreground events");
    for (auto hook : hooks) UnhookWinEvent(hook); hooks.clear();
    std::cout << "Browser tab layout preferences: " << checks << " checks, " << scenarios << " scenarios, " << failures
              << " failures; native windows/show/focus/foreground=" << windows << '/' << shows << '/' << focuses << '/' << foregrounds << ".\n";
    return failures ? 1 : 0;
  } catch (const std::exception &error) {
    for (auto hook : hooks) UnhookWinEvent(hook);
    std::cerr << "Tab-layout fixture/setup failure: " << error.what() << '\n'; return 2;
  }
}
