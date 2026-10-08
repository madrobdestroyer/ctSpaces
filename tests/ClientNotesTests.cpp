#include "../ClientNotes.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using client_notes::ReadStatus;

static void Check(bool condition, const char *description) {
  if (!condition)
    throw std::runtime_error(description);
}

static bool Save(const fs::path &root, const std::wstring &text) {
  return client_notes::Write(root, text, client_notes::Read(root));
}

int main() {
  wchar_t temporary[MAX_PATH]{};
  Check(GetTempPathW(MAX_PATH, temporary) != 0, "Temporary directory");
  const fs::path base = fs::path(temporary) /
      (L"ctSpaces-notes-test-" + std::to_wstring(GetCurrentProcessId()) +
       L"-" + std::to_wstring(GetTickCount64()));
  const fs::path client = base / L"Sites" / L"Example";
  try {
    Check(!Save(client, L"Must not create a client"),
          "Notes do not create a client");
    fs::create_directories(client);
    Check(client_notes::Read(client).status == ReadStatus::Missing,
          "Missing notes have distinct status");
    const std::wstring unicode = L"Caf\u00e9 \u65e5\u672c\U0001f600\r\nLine two\nLine three";
    Check(Save(client, unicode), "Unicode notes save");
    Check(client_notes::Read(client).status == ReadStatus::Ok &&
          client_notes::Read(client).text == unicode,
          "Unicode and mixed newlines round trip");
    Check(Save(client, L""), "Empty notes save");
    Check(client_notes::Read(client).status == ReadStatus::Ok &&
          client_notes::Read(client).text.empty(),
          "Empty note differs from missing note");
    Check(Save(client, unicode), "Notes can be restored after empty save");

    const auto stale = client_notes::Read(client);
    Check(Save(client, L"More recent"), "Concurrent change setup");
    Check(!client_notes::Write(client, L"Stale text", stale) &&
          client_notes::Read(client).text == L"More recent",
          "Stale editor cannot overwrite a newer note");
    Check(!Save(client, std::wstring(client_notes::kMaxBytes + 1, L'x')) &&
          client_notes::Read(client).text == L"More recent",
          "Oversized note preserves prior contents");
    Check(!Save(client, std::wstring(1, static_cast<wchar_t>(0xd800))) &&
          client_notes::Read(client).text == L"More recent",
          "Invalid Unicode preserves prior contents");
    Check(!Save(client, std::wstring(L"before\0after", 12)) &&
          client_notes::Read(client).text == L"More recent",
          "Embedded NUL cannot truncate a note in the editor");
    Check(!client_notes::Write(client, L"Blocked", client_notes::Read(client),
                               [] { return false; }) &&
          client_notes::Read(client).text == L"More recent",
          "Failed root revalidation preserves prior contents");

    const fs::path notePath = client / client_notes::kFileName;
    HANDLE held = CreateFileW(notePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, 0, nullptr);
    Check(held != INVALID_HANDLE_VALUE, "Hold original note");
    const bool blocked = Save(client, L"Blocked replacement");
    CloseHandle(held);
    Check(!blocked && client_notes::Read(client).text == L"More recent",
          "Failed replacement preserves prior contents");

    const fs::path renamed = base / L"Sites" / L"Renamed";
    fs::rename(client, renamed);
    Check(client_notes::Read(renamed).text == L"More recent",
          "Whole client rename preserves notes");
    const fs::path backup = base / L"Backup";
    fs::copy(renamed, backup, fs::copy_options::recursive);
    Check(client_notes::Read(backup).text == L"More recent",
          "Whole client backup preserves notes");

    Check(DeleteFileW((renamed / client_notes::kFileName).c_str()) != FALSE,
          "Remove note for unsafe fixtures");
    {
      std::ofstream file(renamed / client_notes::kFileName,
                         std::ios::binary);
      file << "\xff";
    }
    Check(client_notes::Read(renamed).status == ReadStatus::Error &&
          !Save(renamed, L"Do not erase unreadable notes"),
          "Malformed UTF-8 is never a blank note");
    Check(DeleteFileW((renamed / client_notes::kFileName).c_str()) != FALSE,
          "Remove malformed note");
    {
      std::ofstream file(renamed / client_notes::kFileName,
                         std::ios::binary);
      file.write("before\0after", 12);
    }
    Check(client_notes::Read(renamed).status == ReadStatus::Error &&
          !Save(renamed, L"Do not erase truncated notes"),
          "Embedded NUL on disk is rejected");
    Check(DeleteFileW((renamed / client_notes::kFileName).c_str()) != FALSE,
          "Remove embedded NUL note");
    Check(CreateDirectoryW((renamed / client_notes::kFileName).c_str(), nullptr),
          "Directory fixture");
    Check(client_notes::Read(renamed).status == ReadStatus::Error &&
          !Save(renamed, L"Unsafe"),
          "Directory masquerading as note is rejected");
    Check(RemoveDirectoryW((renamed / client_notes::kFileName).c_str()),
          "Remove directory fixture");
    Check(CreateHardLinkW((renamed / client_notes::kFileName).c_str(),
                          (backup / client_notes::kFileName).c_str(), nullptr),
          "Hard link fixture");
    Check(client_notes::Read(renamed).status == ReadStatus::Error &&
          !Save(renamed, L"Unsafe"),
          "Hard linked note is rejected");

    const fs::path untrusted = base / L"Untrusted";
    fs::create_directory(untrusted);
    const fs::path linkedRoot = base / L"LinkedRoot";
    if (CreateSymbolicLinkW(linkedRoot.c_str(), renamed.c_str(),
                            SYMBOLIC_LINK_FLAG_DIRECTORY |
                                SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
      Check(client_notes::Read(linkedRoot).status == ReadStatus::Error &&
            !Save(linkedRoot, L"Unsafe"),
            "Reparse root is rejected");
    }
    fs::remove_all(base); // Exact unique test-created tree only.
    Check(!fs::exists(base), "Whole client deletion removes notes");
    std::cout << "Client notes: Unicode, newlines, empty, atomic failure, "
                 "unsafe paths, rename, backup, and deletion passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << "\nTest files preserved at " << base << '\n';
    return 1;
  }
}
