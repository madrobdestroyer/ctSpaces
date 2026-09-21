#include "../ConfigPersistence.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using config_persistence::ConfigFileState;
using config_persistence::DirectChildDirectoryState;
using config_persistence::IniMutation;

namespace {

void Expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

class TemporaryDirectory {
public:
  TemporaryDirectory() {
    wchar_t tempPath[MAX_PATH]{};
    const DWORD length = GetTempPathW(MAX_PATH, tempPath);
    if (length == 0 || length >= MAX_PATH)
      throw std::runtime_error("GetTempPathW failed");

    for (unsigned attempt = 0; attempt < 64; ++attempt) {
      path_ = fs::path(tempPath) /
              (L"ctSpaces-config-test-" +
               std::to_wstring(GetCurrentProcessId()) + L"-" +
               std::to_wstring(GetTickCount64()) + L"-" +
               std::to_wstring(attempt));
      if (CreateDirectoryW(path_.c_str(), nullptr))
        return;
      if (GetLastError() != ERROR_ALREADY_EXISTS)
        break;
    }
    throw std::runtime_error("Could not create a unique test directory");
  }

  ~TemporaryDirectory() {
    std::error_code cleanupError;
    fs::remove_all(path_, cleanupError);
  }

  const fs::path &path() const { return path_; }

private:
  fs::path path_;
};

class ScopedHandle {
public:
  explicit ScopedHandle(HANDLE handle) : handle_(handle) {}
  ~ScopedHandle() {
    if (handle_ != INVALID_HANDLE_VALUE)
      CloseHandle(handle_);
  }

  ScopedHandle(const ScopedHandle &) = delete;
  ScopedHandle &operator=(const ScopedHandle &) = delete;

  bool valid() const { return handle_ != INVALID_HANDLE_VALUE; }

private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
};

void WriteBytes(const fs::path &path, const std::string &contents) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  Expect(static_cast<bool>(output), "Could not create test input");
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  output.flush();
  Expect(static_cast<bool>(output), "Could not flush test input");
}

std::string ReadBytes(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  Expect(static_cast<bool>(input), "Could not read test output");
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

bool HasUtf16LeBom(const std::string &contents) {
  return contents.size() >= 2 &&
         static_cast<unsigned char>(contents[0]) == 0xFF &&
         static_cast<unsigned char>(contents[1]) == 0xFE;
}

void ExpectUtf16LeIni(const fs::path &path) {
  const std::string contents = ReadBytes(path);
  Expect(HasUtf16LeBom(contents),
         "The committed configuration is missing its UTF-16LE signature");
  Expect((contents.size() % sizeof(wchar_t)) == 0,
         "The committed UTF-16LE configuration has a partial code unit");
}

std::wstring ReadIniValue(const fs::path &path, const wchar_t *section,
                          const wchar_t *key,
                          const wchar_t *fallback = L"<missing>") {
  wchar_t buffer[512]{};
  GetPrivateProfileStringW(section, key, fallback, buffer,
                           static_cast<DWORD>(std::size(buffer)),
                           path.c_str());
  return buffer;
}

void ExpectNoStagingFiles(const fs::path &directory) {
  for (const auto &entry : fs::directory_iterator(directory)) {
    const std::wstring name = entry.path().filename().wstring();
    Expect(name.find(L".ctspaces-") == std::wstring::npos,
           "A staged configuration file was leaked");
  }
}

void TestSuccessfulAtomicSectionReplacement() {
  TemporaryDirectory temporary;
  const fs::path config = temporary.path() / L"config.ini";
  WriteBytes(config,
             "; keep this compatibility data\r\n"
             "[user]\r\n"
             "theme=2\r\n\r\n"
             "[unknown_plugin]\r\n"
             "opaque=keep-me\r\n\r\n"
             "[pinned]\r\n"
             "count=2\r\n"
             "client0=Alpha\r\n"
             "client1=Beta\r\n");

  std::wstring details;
  const bool updated = config_persistence::ApplyIniMutationsAtomically(
      config,
      {{L"pinned", std::nullopt, std::nullopt},
       {L"pinned", std::wstring(L"count"), std::wstring(L"1")},
       {L"pinned", std::wstring(L"client0"), std::wstring(L"Gamma")}},
      &details);
  if (!updated)
    std::wcerr << L"Atomic update detail: " << details << L'\n';
  Expect(updated, "Atomic INI update failed unexpectedly");
  Expect(ReadIniValue(config, L"pinned", L"count") == L"1",
         "Pinned count was not replaced");
  Expect(ReadIniValue(config, L"pinned", L"client0") == L"Gamma",
         "Pinned client was not replaced");
  Expect(ReadIniValue(config, L"pinned", L"client1") == L"<missing>",
         "A stale pinned key survived section replacement");
  Expect(ReadIniValue(config, L"unknown_plugin", L"opaque") == L"keep-me",
         "An unrelated INI section was not preserved");
  Expect(ReadIniValue(config, L"user", L"theme") == L"2",
         "A legacy user key was not preserved");
  ExpectUtf16LeIni(config);
  ExpectNoStagingFiles(temporary.path());
}

void TestUnicodeRoundTripAndAnsiMigration() {
  TemporaryDirectory temporary;
  const fs::path config = temporary.path() / L"unicode-config.ini";
  WriteBytes(config,
             "; original ANSI configuration\r\n"
             "[unknown_plugin]\r\n"
             "opaque=keep-me\r\n");
  Expect(!HasUtf16LeBom(ReadBytes(config)),
         "The ANSI migration fixture unexpectedly has a Unicode signature");

  const std::wstring unicodeClient =
      L"Client-\u65E5\u672C-\U0001F680";
  std::wstring details;
  const bool updated = config_persistence::ApplyIniMutationsAtomically(
      config,
      {{L"pinned", std::nullopt, std::nullopt},
       {L"pinned", std::wstring(L"count"), std::wstring(L"1")},
       {L"pinned", std::wstring(L"client0"), unicodeClient}},
      &details);
  if (!updated)
    std::wcerr << L"Unicode migration detail: " << details << L'\n';
  Expect(updated, "An ANSI config could not be migrated and updated");
  ExpectUtf16LeIni(config);
  Expect(ReadIniValue(config, L"pinned", L"client0") == unicodeClient,
         "A non-ACP client name did not round-trip through config.ini");
  Expect(ReadIniValue(config, L"unknown_plugin", L"opaque") == L"keep-me",
         "ANSI migration lost an unrelated configuration value");

  Expect(config_persistence::ApplyIniMutationsAtomically(
             config,
             {{L"user", std::wstring(L"client_title_first"),
               std::wstring(L"1")}},
             &details),
         "An existing UTF-16LE config could not be updated again");
  ExpectUtf16LeIni(config);
  Expect(ReadIniValue(config, L"pinned", L"client0") == unicodeClient,
         "A later config update corrupted the Unicode client name");
  Expect(ReadIniValue(config, L"user", L"client_title_first") == L"1",
         "The later UTF-16LE config update stored the wrong value");
  ExpectNoStagingFiles(temporary.path());
}

void TestMalformedUtf16IsRejectedBeforeMutation() {
  TemporaryDirectory temporary;
  const fs::path config = temporary.path() / L"malformed-unicode.ini";
  std::string original("\xFF\xFE", 2);
  original.append("\x5B\x00\x78", 3);
  WriteBytes(config, original);

  bool callbackRan = false;
  std::wstring details;
  const bool updated = config_persistence::MutateIniFileAtomically(
      config,
      [&callbackRan](const fs::path &, std::wstring &) {
        callbackRan = true;
        return true;
      },
      &details);
  Expect(!updated, "A partial UTF-16 code unit was accepted");
  Expect(!callbackRan, "Mutation ran against malformed UTF-16 input");
  Expect(details.find(L"partial code unit") != std::wstring::npos,
         "Malformed UTF-16 rejection detail was lost");
  Expect(ReadBytes(config) == original,
         "Malformed UTF-16 rejection changed the original bytes");

  const fs::path nullConfig = temporary.path() / L"nul-unicode.ini";
  const std::string embeddedNull("\xFF\xFE\x5B\x00\x00\x00\x5D\x00", 8);
  WriteBytes(nullConfig, embeddedNull);
  callbackRan = false;
  details.clear();
  Expect(!config_persistence::MutateIniFileAtomically(
             nullConfig,
             [&callbackRan](const fs::path &, std::wstring &) {
               callbackRan = true;
               return true;
             },
             &details),
         "Embedded UTF-16 null input was accepted");
  Expect(!callbackRan, "Mutation ran against embedded UTF-16 null input");
  Expect(ReadBytes(nullConfig) == embeddedNull,
         "Embedded-null rejection changed the original bytes");
  ExpectNoStagingFiles(temporary.path());
}

void TestUtf8BomMigrationPreservesUnicodeSections() {
  TemporaryDirectory temporary;
  const fs::path config = temporary.path() / L"utf8-config.ini";
  std::string original("\xEF\xBB\xBF", 3);
  original += "[unknown_plugin]\r\nname=caf";
  original.append("\xC3\xA9", 2);
  original += "\r\n";
  WriteBytes(config, original);

  std::wstring details;
  const bool updated = config_persistence::ApplyIniMutationsAtomically(
      config,
      {{L"user", std::wstring(L"browser"), std::wstring(L"firefox")}},
      &details);
  if (!updated)
    std::wcerr << L"UTF-8 migration detail: " << details << L'\n';
  Expect(updated, "A BOM-marked UTF-8 config could not be migrated");
  ExpectUtf16LeIni(config);
  Expect(ReadIniValue(config, L"unknown_plugin", L"name") ==
             L"caf\u00E9",
         "UTF-8 migration corrupted or hid the first Unicode section");
  Expect(ReadIniValue(config, L"user", L"browser") == L"firefox",
         "The requested mutation was lost during UTF-8 migration");
  ExpectNoStagingFiles(temporary.path());
}

void TestMalformedUtf8AndUtf32AreRejected() {
  TemporaryDirectory temporary;
  const fs::path utf8Config = temporary.path() / L"invalid-utf8.ini";
  std::string invalidUtf8("\xEF\xBB\xBF", 3);
  invalidUtf8 += "[user]\r\nname=";
  invalidUtf8.push_back(static_cast<char>(0xC3));
  WriteBytes(utf8Config, invalidUtf8);

  bool callbackRan = false;
  std::wstring details;
  Expect(!config_persistence::MutateIniFileAtomically(
             utf8Config,
             [&callbackRan](const fs::path &, std::wstring &) {
               callbackRan = true;
               return true;
             },
             &details),
         "Malformed BOM-marked UTF-8 was accepted");
  Expect(!callbackRan, "Mutation ran against malformed UTF-8 input");
  Expect(ReadBytes(utf8Config) == invalidUtf8,
         "Malformed UTF-8 rejection changed the original bytes");

  const fs::path utf32Config = temporary.path() / L"utf32.ini";
  const std::string utf32Le("\xFF\xFE\x00\x00[\x00\x00\x00", 8);
  WriteBytes(utf32Config, utf32Le);
  callbackRan = false;
  details.clear();
  Expect(!config_persistence::MutateIniFileAtomically(
             utf32Config,
             [&callbackRan](const fs::path &, std::wstring &) {
               callbackRan = true;
               return true;
             },
             &details),
         "UTF-32 input was accepted as UTF-16");
  Expect(!callbackRan, "Mutation ran against unsupported UTF-32 input");
  Expect(ReadBytes(utf32Config) == utf32Le,
         "UTF-32 rejection changed the original bytes");
  ExpectNoStagingFiles(temporary.path());
}

void TestUtf32BigEndianIsRejectedWithoutMutation() {
  TemporaryDirectory temporary;
  const fs::path config = temporary.path() / L"utf32-big-endian.ini";
  const std::string original("\x00\x00\xFE\xFF\x00\x00\x00\x5B", 8);
  WriteBytes(config, original);

  bool callbackRan = false;
  std::wstring details;
  Expect(!config_persistence::MutateIniFileAtomically(
             config,
             [&callbackRan](const fs::path &, std::wstring &) {
               callbackRan = true;
               return true;
             },
             &details),
         "Big-endian UTF-32 input was accepted");
  Expect(!callbackRan, "Mutation ran against big-endian UTF-32 input");
  Expect(details.find(L"unsupported UTF-32 text") != std::wstring::npos,
         "Big-endian UTF-32 rejection detail was lost");
  Expect(ReadBytes(config) == original,
         "Big-endian UTF-32 rejection changed the original bytes");
  ExpectNoStagingFiles(temporary.path());
}

void TestEmbeddedNullTextInputsAreRejectedWithoutMutation() {
  TemporaryDirectory temporary;

  const fs::path utf8Config = temporary.path() / L"nul-utf8.ini";
  std::string utf8Original("\xEF\xBB\xBF", 3);
  utf8Original += "[user]\r\nname=A";
  utf8Original.push_back('\0');
  utf8Original += "B\r\n";
  WriteBytes(utf8Config, utf8Original);

  bool callbackRan = false;
  std::wstring details;
  Expect(!config_persistence::MutateIniFileAtomically(
             utf8Config,
             [&callbackRan](const fs::path &, std::wstring &) {
               callbackRan = true;
               return true;
             },
             &details),
         "Embedded-null BOM-marked UTF-8 input was accepted");
  Expect(!callbackRan,
         "Mutation ran against embedded-null BOM-marked UTF-8 input");
  Expect(details.find(L"embedded null character") != std::wstring::npos,
         "Embedded-null UTF-8 rejection detail was lost");
  Expect(ReadBytes(utf8Config) == utf8Original,
         "Embedded-null UTF-8 rejection changed the original bytes");

  const fs::path ansiConfig = temporary.path() / L"nul-ansi.ini";
  std::string ansiOriginal = "[user]\r\nname=A";
  ansiOriginal.push_back('\0');
  ansiOriginal += "B\r\n";
  WriteBytes(ansiConfig, ansiOriginal);

  callbackRan = false;
  details.clear();
  Expect(!config_persistence::MutateIniFileAtomically(
             ansiConfig,
             [&callbackRan](const fs::path &, std::wstring &) {
               callbackRan = true;
               return true;
             },
             &details),
         "Embedded-null ANSI input was accepted");
  Expect(!callbackRan, "Mutation ran against embedded-null ANSI input");
  Expect(details.find(L"embedded null character") != std::wstring::npos,
         "Embedded-null ANSI rejection detail was lost");
  Expect(ReadBytes(ansiConfig) == ansiOriginal,
         "Embedded-null ANSI rejection changed the original bytes");
  ExpectNoStagingFiles(temporary.path());
}

void TestAtomicReplaceSharingViolationPreservesOriginal() {
  TemporaryDirectory temporary;
  const fs::path config = temporary.path() / L"locked-config.ini";
  const std::string original =
      "[user]\r\nbrowser=edge\r\n\r\n"
      "[unknown]\r\nvalue=byte-exact\r\n";
  WriteBytes(config, original);

  std::wstring details;
  {
    ScopedHandle destinationLock(CreateFileW(
        config.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    Expect(destinationLock.valid(),
           "Could not lock config.ini against atomic replacement");

    const bool updated = config_persistence::ApplyIniMutationsAtomically(
        config,
        {{L"user", std::wstring(L"browser"), std::wstring(L"firefox")}},
        &details);
    Expect(!updated,
           "Atomic config replacement ignored a missing delete share");
    Expect(details.find(L"could not replace config.ini") !=
               std::wstring::npos,
           "Atomic replacement failure detail was lost");
    // MoveFileExW reports a destination that denies FILE_SHARE_DELETE as
    // either ERROR_ACCESS_DENIED or ERROR_SHARING_VIOLATION across supported
    // Windows/filesystem combinations.
    const bool reportedDeleteShareConflict =
        details.find(L"Windows error " +
                     std::to_wstring(ERROR_ACCESS_DENIED)) !=
            std::wstring::npos ||
        details.find(L"Windows error " +
                     std::to_wstring(ERROR_SHARING_VIOLATION)) !=
            std::wstring::npos;
    if (!reportedDeleteShareConflict) {
      std::wcerr << L"Atomic replacement detail: " << details << L'\n';
    }
    Expect(reportedDeleteShareConflict,
           "Atomic replacement did not report the delete-share conflict");
    Expect(ReadBytes(config) == original,
           "A failed atomic replacement changed the original config bytes");
    ExpectNoStagingFiles(temporary.path());
  }

  details.clear();
  Expect(config_persistence::ApplyIniMutationsAtomically(
             config,
             {{L"user", std::wstring(L"browser"),
               std::wstring(L"firefox")}},
             &details),
         "A clean retry failed after the sharing violation was released");
  Expect(ReadIniValue(config, L"user", L"browser") == L"firefox",
         "The clean retry stored the wrong config value");
  Expect(ReadIniValue(config, L"unknown", L"value") == L"byte-exact",
         "The clean retry lost an unrelated config value");
  ExpectUtf16LeIni(config);
  ExpectNoStagingFiles(temporary.path());
}

void TestRejectedMutationPreservesOriginalBytes() {
  TemporaryDirectory temporary;
  const fs::path config = temporary.path() / L"config.ini";
  const std::string original =
      "[pinned]\r\ncount=1\r\nclient0=Original\r\n\r\n"
      "[unknown]\r\nvalue=untouched\r\n";
  WriteBytes(config, original);

  std::wstring details;
  const bool updated = config_persistence::MutateIniFileAtomically(
      config,
      [](const fs::path &stage, std::wstring &error) {
        const BOOL firstWrite = WritePrivateProfileStringW(
            L"pinned", L"client0", L"Partial", stage.c_str());
        Expect(firstWrite != FALSE, "The injected first write failed");
        error = L"Injected failure after the first staged write.";
        return false;
      },
      &details);
  Expect(!updated, "An explicitly rejected mutation was committed");
  Expect(details.find(L"Injected failure") != std::wstring::npos,
         "The mutation failure detail was lost");
  Expect(ReadBytes(config) == original,
         "A failed transaction changed the original config bytes");
  ExpectNoStagingFiles(temporary.path());
}

void TestMissingFileCreationAndUnsafeTargetRejection() {
  TemporaryDirectory temporary;
  const fs::path config = temporary.path() / L"new-config.ini";
  Expect(config_persistence::InspectConfigFile(config) ==
             ConfigFileState::Missing,
         "A missing config was not classified as missing");

  std::wstring details;
  Expect(config_persistence::ApplyIniMutationsAtomically(
             config,
             {{L"user", std::wstring(L"browser"),
               std::wstring(L"firefox")}},
             &details),
         "A missing config could not be created atomically");
  Expect(config_persistence::InspectConfigFile(config) ==
             ConfigFileState::Regular,
         "The created config was not a regular readable file");
  Expect(ReadIniValue(config, L"user", L"browser") == L"firefox",
         "The created config has the wrong value");
  ExpectUtf16LeIni(config);

  const fs::path unsafeTarget = temporary.path() / L"unsafe.ini";
  Expect(CreateDirectoryW(unsafeTarget.c_str(), nullptr) != FALSE,
         "Could not create the unsafe target fixture");
  Expect(config_persistence::InspectConfigFile(unsafeTarget) ==
             ConfigFileState::Unavailable,
         "A directory config target was not rejected");
  Expect(!config_persistence::ApplyIniMutationsAtomically(
             unsafeTarget,
             {{L"user", std::wstring(L"theme"), std::wstring(L"1")}},
             &details),
         "An unsafe config target was overwritten");
  Expect(fs::is_directory(unsafeTarget),
         "The unsafe target directory was modified");
  ExpectNoStagingFiles(temporary.path());
}

void TestIndeterminateProbeSupportsAllOrNothingPruning() {
  TemporaryDirectory temporary;
  const fs::path sites = temporary.path() / L"Sites";
  Expect(CreateDirectoryW(sites.c_str(), nullptr) != FALSE,
         "Could not create the Sites fixture");
  Expect(CreateDirectoryW((sites / L"Alpha").c_str(), nullptr) != FALSE,
         "Could not create a client fixture");

  std::wstring resolved;
  Expect(config_persistence::ProbeDirectChildDirectory(
             sites, L"alpha", resolved) ==
             DirectChildDirectoryState::Present,
         "A present client directory was not detected");
  Expect(_wcsicmp(resolved.c_str(), L"Alpha") == 0,
         "The client name was not resolved with disk casing");
  Expect(config_persistence::ProbeDirectChildDirectory(
             sites, L"Missing", resolved) ==
             DirectChildDirectoryState::MissingOrInvalid,
         "A missing client was not classified definitively");

  const fs::path unavailableRoot = temporary.path() / L"SitesUnavailable";
  WriteBytes(unavailableRoot, "not a directory");
  const std::vector<std::wstring> original{L"Alpha", L"KeepOnError"};
  std::vector<std::wstring> pruned = original;
  const auto probe = config_persistence::ProbeDirectChildDirectory(
      unavailableRoot, L"KeepOnError", resolved);
  if (probe != DirectChildDirectoryState::Indeterminate)
    pruned.clear();
  Expect(probe == DirectChildDirectoryState::Indeterminate,
         "An unsafe/unavailable client root was treated as missing");
  Expect(pruned == original,
         "Indeterminate filesystem state was allowed to prune preferences");
}

} // namespace

int wmain() {
  try {
    TestSuccessfulAtomicSectionReplacement();
    TestUnicodeRoundTripAndAnsiMigration();
    TestMalformedUtf16IsRejectedBeforeMutation();
    TestUtf8BomMigrationPreservesUnicodeSections();
    TestMalformedUtf8AndUtf32AreRejected();
    TestUtf32BigEndianIsRejectedWithoutMutation();
    TestEmbeddedNullTextInputsAreRejectedWithoutMutation();
    TestAtomicReplaceSharingViolationPreservesOriginal();
    TestRejectedMutationPreservesOriginalBytes();
    TestMissingFileCreationAndUnsafeTargetRejection();
    TestIndeterminateProbeSupportsAllOrNothingPruning();
    std::wcout << L"Config persistence tests passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Config persistence test failure: " << error.what() << '\n';
    return 1;
  }
}
