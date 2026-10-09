#include "../ClientNotes.h"
#include <richedit.h>
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

static void PutBytes(const fs::path &path, const std::string &bytes) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  Check(file.good(), "Open byte fixture");
  file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  Check(file.good(), "Write byte fixture");
}

static void TestNotebook(const fs::path &base) {
  using client_notes::NotePage;
  const std::string rtf = "{\\rtf1\\ansi A \\b note\\b0}";
  const fs::path root = base / L"Sites" / L"Notebook";
  fs::create_directory(root);
  const fs::path path = root / client_notes::kNotebookFileName;
  const auto missing = client_notes::ReadNotebook(root);
  Check(missing.status == ReadStatus::Missing && missing.pages.size() == 1 &&
            missing.pages[0].name == L"Notes" &&
            !fs::exists(path),
        "Missing notebook exposes one logical Notes tab without a file");
  std::vector<NotePage> pages{{L"Notes", true, rtf, {}}};
  Check(client_notes::WriteNotebook(root, pages, missing),
        "First notebook save creates one file");
  const auto saved = client_notes::ReadNotebook(root);
  Check(saved.status == ReadStatus::Ok && saved.pages.size() == 1 &&
            saved.pages[0].name == L"Notes" &&
            saved.pages[0].rich && saved.pages[0].rtf == rtf &&
            !fs::exists(root / client_notes::kFileName) &&
            !fs::exists(root / client_notes::kRichFileName),
        "Notebook RTF round trips without legacy files");
  pages.push_back({L"Follow up \U0001f600", true, rtf, {}});
  Check(client_notes::WriteNotebook(root, pages, saved) &&
            client_notes::ReadNotebook(root).pages.size() == 2,
        "Unicode tab name and second tab round trip");
  Check(!client_notes::WriteNotebook(root, pages, saved),
        "Stale notebook snapshot cannot overwrite added tab");
  const auto twoTabs = client_notes::ReadNotebook(root);
  HANDLE held = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, 0, nullptr);
  Check(held != INVALID_HANDLE_VALUE, "Hold original notebook");
  const bool blocked = client_notes::WriteNotebook(root, pages, twoTabs);
  CloseHandle(held);
  Check(!blocked && client_notes::ReadNotebook(root).pages.size() == 2,
        "Failed notebook replacement preserves tabs");
  Check(!client_notes::WriteNotebook(root, pages, twoTabs,
                                     [] { return false; }),
        "Notebook root revalidation blocks save");

  for (const std::wstring bad : {
           std::wstring(L""), std::wstring(L" leading"),
           std::wstring(L"trailing "), std::wstring(L"bad\nname"),
           std::wstring(65, L'x'),
           std::wstring(1, static_cast<wchar_t>(0xd800))}) {
    pages[1].name = bad;
    Check(!client_notes::WriteNotebook(root, pages, twoTabs) &&
              client_notes::ReadNotebook(root).pages.size() == 2,
          "Invalid tab name preserves notebook");
  }
  pages[1].name = L"notes";
  Check(!client_notes::WriteNotebook(root, pages, twoTabs),
        "Case-insensitive duplicate tab names are rejected");
  pages[1].name = L"Follow up";
  pages[1].rtf = "{\\rtf1{\\object unsafe}}";
  Check(!client_notes::WriteNotebook(root, pages, twoTabs),
        "Unsafe page RTF is rejected");
  pages[1].rtf = rtf;
  pages[1].rich = false;
  Check(!client_notes::WriteNotebook(root, pages, twoTabs),
        "Unsaved plain page must be converted before persistence");
  pages[1].rich = true;

  std::vector<NotePage> maxPages;
  for (size_t i = 0; i < client_notes::kMaxPages; ++i)
    maxPages.push_back({L"Tab " + std::to_wstring(i), true, rtf, {}});
  Check(client_notes::WriteNotebook(root, maxPages, twoTabs) &&
            client_notes::ReadNotebook(root).pages.size() == client_notes::kMaxPages,
        "256 tab boundary saves and reads");
  const auto full = client_notes::ReadNotebook(root);
  maxPages.push_back({L"One too many", true, rtf, {}});
  Check(!client_notes::WriteNotebook(root, maxPages, full),
        "257th tab is rejected");
  {
    std::ofstream oversized(path, std::ios::binary | std::ios::trunc);
    oversized.seekp(client_notes::kMaxNotebookBytes);
    oversized.put('x');
  }
  Check(client_notes::ReadNotebook(root).status == ReadStatus::Error,
        "Oversized notebook is rejected before allocation");
  PutBytes(path, full.notebook_snapshot.bytes);

  const std::string goodBytes = full.notebook_snapshot.bytes;
  for (const std::string bad : {
           goodBytes.substr(0, goodBytes.size() - 1),
           goodBytes + "x",
           std::string("badmagic") + goodBytes.substr(8),
           goodBytes.substr(0, 8) + std::string("\2\0\0\0", 4) +
               goodBytes.substr(12),
           std::string("CTNBOOK1\1\0\0\0\xff\xff\xff\xff", 16)}) {
    PutBytes(path, bad);
    Check(client_notes::ReadNotebook(root).status == ReadStatus::Error &&
              !client_notes::WriteNotebook(
                  root, pages, client_notes::ReadNotebook(root)),
          "Malformed notebook never falls back to legacy data");
  }
  std::string badName = goodBytes;
  badName[24] = ' ';
  PutBytes(path, badName);
  Check(client_notes::ReadNotebook(root).status == ReadStatus::Error,
        "Malformed stored page name is rejected");
  std::string duplicate = "CTNBOOK1";
  client_notes::AppendU32(duplicate, 1);
  client_notes::AppendU32(duplicate, 2);
  for (int i = 0; i < 2; ++i) {
    client_notes::AppendU32(duplicate, 5);
    client_notes::AppendU32(duplicate, static_cast<uint32_t>(rtf.size()));
    duplicate += i ? "notes" : "Notes";
    duplicate += rtf;
  }
  PutBytes(path, duplicate);
  Check(client_notes::ReadNotebook(root).status == ReadStatus::Error,
        "Duplicate stored page names are rejected");
  PutBytes(path, goodBytes);
  const fs::path renamed = base / L"Sites" / L"NotebookRenamed";
  fs::rename(root, renamed);
  Check(client_notes::ReadNotebook(renamed).pages.size() == client_notes::kMaxPages,
        "Notebook follows whole client rename");
  const fs::path backup = base / L"NotebookBackup";
  fs::copy(renamed, backup, fs::copy_options::recursive);
  Check(client_notes::ReadNotebook(backup).pages.size() == client_notes::kMaxPages,
        "Notebook follows whole client backup");
  const fs::path renamedNotebook = renamed / client_notes::kNotebookFileName;
  Check(DeleteFileW(renamedNotebook.c_str()) != FALSE &&
            CreateHardLinkW(renamedNotebook.c_str(),
                            (backup / client_notes::kNotebookFileName).c_str(),
                            nullptr),
        "Notebook hard link fixture");
  Check(client_notes::ReadNotebook(renamed).status == ReadStatus::Error,
        "Hard linked notebook is rejected");
  const fs::path legacy = base / L"Sites" / L"NotebookLegacy";
  fs::create_directory(legacy);
  const fs::path txtPath = legacy / client_notes::kFileName;
  const fs::path rtfPath = legacy / client_notes::kRichFileName;
  PutBytes(txtPath, "Legacy TXT");
  const auto inheritedPlain = client_notes::ReadNotebook(legacy);
  Check(inheritedPlain.status == ReadStatus::Ok &&
            inheritedPlain.pages.size() == 1 &&
            !inheritedPlain.pages[0].rich &&
            inheritedPlain.pages[0].text == L"Legacy TXT" &&
            !fs::exists(legacy / client_notes::kNotebookFileName),
        "Legacy TXT lazily becomes Notes tab");
  Check(client_notes::WriteNotebook(legacy, pages, inheritedPlain) &&
            client_notes::Read(legacy).text == L"Legacy TXT",
        "Notebook save preserves legacy TXT");
  const auto beforeTxtChange = client_notes::ReadNotebook(legacy);
  Check(Save(legacy, L"Changed TXT"), "Change legacy TXT after notebook");
  Check(!client_notes::WriteNotebook(legacy, pages, beforeTxtChange),
        "Legacy TXT change invalidates notebook editor");
  PutBytes(rtfPath, rtf);
  const auto beforeRtfChange = client_notes::ReadNotebook(legacy);
  PutBytes(rtfPath, "{\\rtf1 changed}");
  Check(!client_notes::WriteNotebook(legacy, pages, beforeRtfChange),
        "Legacy RTF change invalidates notebook editor");
  Check(client_notes::ReadNotebook(legacy).pages.size() == 2,
        "Canonical notebook takes precedence over legacy files");
  const fs::path legacyNotebook = legacy / client_notes::kNotebookFileName;
  const std::string canonical =
      client_notes::ReadNotebook(legacy).notebook_snapshot.bytes;
  PutBytes(legacyNotebook, "truncated");
  Check(client_notes::ReadNotebook(legacy).status == ReadStatus::Error,
        "Malformed canonical notebook never falls back to valid legacy files");
  PutBytes(legacyNotebook, canonical);
  const fs::path oldRich = base / L"Sites" / L"NotebookOldRich";
  fs::create_directory(oldRich);
  PutBytes(oldRich / client_notes::kRichFileName, rtf);
  const auto inheritedRich = client_notes::ReadNotebook(oldRich);
  Check(inheritedRich.status == ReadStatus::Ok &&
            inheritedRich.pages.size() == 1 &&
            inheritedRich.pages[0].rich && inheritedRich.pages[0].rtf == rtf,
        "Legacy RTF lazily becomes Notes tab");
}

static DWORD CALLBACK ProbeStream(DWORD_PTR cookie, LPBYTE bytes, LONG count,
                                  LONG *written) {
  reinterpret_cast<std::string *>(cookie)->append(
      reinterpret_cast<char *>(bytes), static_cast<size_t>(count));
  *written = count;
  return 0;
}

static DWORD CALLBACK ProbeRead(DWORD_PTR cookie, LPBYTE bytes, LONG count,
                               LONG *read) {
  auto &input = *reinterpret_cast<std::string_view *>(cookie);
  const size_t length = (std::min)(input.size(), static_cast<size_t>(count));
  memcpy(bytes, input.data(), length);
  input.remove_prefix(length);
  *read = static_cast<LONG>(length);
  return 0;
}

static void TestNativeRichText() {
  HMODULE module = LoadLibraryW(L"Msftedit.dll");
  Check(module != nullptr, "Native rich editor loads");
  HWND edit = CreateWindowExW(0, MSFTEDIT_CLASS, L"Native editor text", WS_POPUP |
      ES_MULTILINE, 0, 0, 500, 300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  Check(edit != nullptr, "Native rich editor creates");
  std::string bytes;
  EDITSTREAM stream{reinterpret_cast<DWORD_PTR>(&bytes), 0, ProbeStream};
  SendMessageW(edit, EM_STREAMOUT, SF_RTF, reinterpret_cast<LPARAM>(&stream));
  Check(!bytes.empty() && bytes.back() == '\0', "Native RTF exports its terminator");
  Check(client_notes::NormalizeEditorExport(bytes), "Normalize native editor export");
  if (!client_notes::ValidRichText(bytes)) {
    std::cerr << "Native RTF bytes=" << bytes.size() << " tail:";
    for (size_t i = bytes.size() > 40 ? bytes.size() - 40 : 0; i < bytes.size(); ++i)
      std::cerr << ' ' << static_cast<unsigned>(static_cast<unsigned char>(bytes[i]));
    std::cerr << '\n';
  }
  Check(!stream.dwError && client_notes::ValidRichText(bytes),
        "Native editor RTF passes the storage gate");
  SendMessageW(edit, EM_AUTOURLDETECT, AURL_ENABLEURL, 0);
  SetWindowTextW(edit, L"https://example.com/ticket");
  SendMessageW(edit, EM_SETSEL, 0, -1);
  CHARFORMAT2W link{};
  link.cbSize = sizeof(link);
  link.dwMask = CFM_LINK | CFM_UNDERLINE | CFM_BOLD;
  link.dwEffects = CFE_LINK | CFE_UNDERLINE | CFE_BOLD;
  SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&link));
  bytes.clear();
  stream.dwError = 0;
  SendMessageW(edit, EM_STREAMOUT, SF_RTF, reinterpret_cast<LPARAM>(&stream));
  Check(client_notes::NormalizeEditorExport(bytes), "Normalize automatic URL fields");
  Check(!stream.dwError && client_notes::ValidRichText(bytes),
        "Native linked and formatted text passes the storage gate");
  Check(bytes.find("\\fldinst") == std::string::npos &&
        bytes.find("https://example.com/ticket") != std::string::npos,
        "Link export preserves visible URL without field instructions");
  SetWindowTextW(edit, L"\u2611\u2003 Completed task");
  bytes.clear(); stream.dwError = 0;
  SendMessageW(edit, EM_STREAMOUT, SF_RTF, reinterpret_cast<LPARAM>(&stream));
  Check(client_notes::NormalizeEditorExport(bytes), "Normalize checklist spacing");
  SetWindowTextW(edit, L"");
  std::string_view input(bytes);
  EDITSTREAM reader{reinterpret_cast<DWORD_PTR>(&input), 0, ProbeRead};
  SendMessageW(edit, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&reader));
  wchar_t restored[64]{};
  GetWindowTextW(edit, restored, 64);
  Check(!reader.dwError && std::wstring_view(restored) == L"\u2611\u2003 Completed task",
        "Native checklist marker and em-space survive save/reopen");
  std::string legacy = "{\\rtf1\\ansi\\uc0\\emspace text \\\\emspace}";
  Check(client_notes::PreserveEditorSpacing(legacy), "Repair legacy spacing tokens");
  Check(legacy == "{\\rtf1\\ansi\\uc0{\\uc1\\u8195?}text \\\\emspace}",
        "Spacing normalization consumes delimiter, scopes Unicode fallback, preserves literal slash");
  const auto normalized = legacy;
  Check(client_notes::PreserveEditorSpacing(legacy) && legacy == normalized,
        "Spacing normalization is idempotent");
  DestroyWindow(edit);
  FreeLibrary(module);
}

int main() {
  TestNativeRichText();
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

    TestNotebook(base);

    const fs::path richClient = base / L"Sites" / L"Rich";
    fs::create_directory(richClient);
    const fs::path richPath = richClient / client_notes::kRichFileName;
    const fs::path legacyPath = richClient / client_notes::kFileName;
    const auto emptyDocument = client_notes::ReadDocument(richClient);
    Check(emptyDocument.status == ReadStatus::Missing &&
          !fs::exists(richPath) && !fs::exists(legacyPath),
          "Opening a missing rich note creates no files");
    const std::string firstRtf = "{\\rtf1\\ansi\\deff0 Hello \\b world\\b0\\par}";
    Check(client_notes::WriteDocument(richClient, firstRtf, emptyDocument),
          "New rich note saves");
    Check(client_notes::ReadDocument(richClient).rich &&
          client_notes::ReadDocument(richClient).rtf == firstRtf &&
          !fs::exists(legacyPath), "Rich note round trips without a TXT file");

    const auto beforeBadSave = client_notes::ReadDocument(richClient);
    for (const std::string invalid : {
             std::string("{\\rtf1\\ansi {\\object\\objdata 123}}"),
             std::string("{\\rtf1{\\OBJECT unsafe}}"),
             std::string("{\\rtf1{\\pict 0102}}"),
             std::string("{\\rtf1{\\field{\\*\\fldinst HYPERLINK x}}}"),
             std::string("{\\rtf1\\bin4 abcd}"),
             std::string("{\\rtf1 broken"),
             std::string("{\\rtf1 before\0after}", 20),
             std::string(client_notes::kMaxRichBytes + 1, 'x')}) {
      Check(!client_notes::WriteDocument(richClient, invalid, beforeBadSave) &&
            client_notes::ReadDocument(richClient).rtf == firstRtf,
            "Unsafe or oversized RTF preserves prior contents");
    }
    Check(!client_notes::WriteDocument(richClient, "{\\rtf1 new}",
                                      beforeBadSave, [] { return false; }) &&
          client_notes::ReadDocument(richClient).rtf == firstRtf,
          "Failed rich root revalidation preserves prior contents");

    const std::string secondRtf = "{\\rtf1\\ansi Updated}";
    Check(client_notes::WriteDocument(richClient, secondRtf,
                                      client_notes::ReadDocument(richClient)),
          "Rich note update saves");
    Check(!client_notes::WriteDocument(richClient, firstRtf, beforeBadSave) &&
          client_notes::ReadDocument(richClient).rtf == secondRtf,
          "Stale rich editor cannot replace newer RTF");
    HANDLE heldRich = CreateFileW(richPath.c_str(), GENERIC_READ,
                                  FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0,
                                  nullptr);
    Check(heldRich != INVALID_HANDLE_VALUE, "Hold original rich note");
    const bool blockedRich = client_notes::WriteDocument(
        richClient, firstRtf, client_notes::ReadDocument(richClient));
    CloseHandle(heldRich);
    Check(!blockedRich && client_notes::ReadDocument(richClient).rtf == secondRtf,
          "Failed rich replacement preserves prior contents");

    const fs::path legacyClient = base / L"Sites" / L"Legacy";
    fs::create_directory(legacyClient);
    const fs::path legacyTextPath = legacyClient / client_notes::kFileName;
    const fs::path legacyRichPath = legacyClient / client_notes::kRichFileName;
    PutBytes(legacyTextPath, "Legacy text");
    const auto legacyDocument = client_notes::ReadDocument(legacyClient);
    Check(legacyDocument.status == ReadStatus::Ok && !legacyDocument.rich &&
          legacyDocument.text == L"Legacy text" &&
          !fs::exists(legacyRichPath), "Legacy TXT opens without migration");
    Check(client_notes::WriteDocument(legacyClient, firstRtf, legacyDocument) &&
          fs::exists(legacyRichPath) &&
          client_notes::Read(legacyClient).text == L"Legacy text",
          "First rich save preserves the legacy TXT file");
    const auto beforeLegacyChange = client_notes::ReadDocument(legacyClient);
    Check(Save(legacyClient, L"Other process changed TXT"),
          "Concurrent legacy change setup");
    Check(!client_notes::WriteDocument(legacyClient, secondRtf,
                                       beforeLegacyChange) &&
          client_notes::ReadDocument(legacyClient).rtf == firstRtf,
          "Stale rich editor cannot overwrite after TXT change");
    Check(client_notes::WriteDocument(
              legacyClient, secondRtf, client_notes::ReadDocument(legacyClient)),
          "Fresh rich editor can save after TXT change");
    PutBytes(legacyRichPath, "{\\rtf1{\\object unsafe}}");
    Check(client_notes::ReadDocument(legacyClient).status == ReadStatus::Error &&
          !client_notes::WriteDocument(legacyClient, firstRtf,
                                       client_notes::ReadDocument(legacyClient)),
          "Unsafe existing RTF is never hidden by legacy TXT");
    PutBytes(legacyRichPath, "{\\rtf1 missing brace");
    Check(client_notes::ReadDocument(legacyClient).status == ReadStatus::Error,
          "Malformed existing RTF is never hidden by legacy TXT");
    PutBytes(legacyRichPath,
             std::string(client_notes::kMaxRichBytes + 1, 'x'));
    Check(client_notes::ReadDocument(legacyClient).status == ReadStatus::Error,
          "Oversized existing RTF is never hidden by legacy TXT");
    Check(DeleteFileW(legacyRichPath.c_str()) != FALSE,
          "Remove unsafe RTF for hard link fixture");
    Check(CreateHardLinkW(legacyRichPath.c_str(), richPath.c_str(), nullptr),
          "Rich hard link fixture");
    Check(client_notes::ReadDocument(legacyClient).status == ReadStatus::Error &&
          !client_notes::WriteDocument(legacyClient, firstRtf,
                                       client_notes::ReadDocument(legacyClient)),
          "Hard linked RTF is rejected");

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
