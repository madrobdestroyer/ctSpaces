#pragma once

#include <windows.h>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

// Notes belong to the whole client container. Moving or backing up that
// container moves the notes with it; opening the editor never creates one.
namespace client_notes {
inline constexpr wchar_t kFileName[] = L"ctSpaces-client-notes.txt";
inline constexpr wchar_t kRichFileName[] = L"ctSpaces-client-notes.rtf";
inline constexpr wchar_t kNotebookFileName[] = L"ctSpaces-client-notes.ctn";
inline constexpr DWORD kMaxBytes = 32 * 1024 * 1024;
inline constexpr DWORD kMaxRichBytes = 64 * 1024 * 1024;
inline constexpr DWORD kMaxNotebookBytes = 256 * 1024 * 1024;
inline constexpr size_t kMaxPages = 256;
inline std::mutex mutex;

enum class ReadStatus { Ok, Missing, Error };
struct ReadResult {
  ReadStatus status = ReadStatus::Error;
  std::wstring text;
};

inline bool SafeDirectory(const std::filesystem::path &root) {
  const DWORD attributes = GetFileAttributesW(root.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
         (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

inline bool SafeFileHandle(HANDLE file, DWORD &size, DWORD maxBytes = kMaxBytes) {
  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(file, &info) ||
      (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY |
                                FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
      info.nNumberOfLinks != 1 || info.nFileSizeHigh != 0 ||
      info.nFileSizeLow > maxBytes || GetFileType(file) != FILE_TYPE_DISK)
    return false;
  size = info.nFileSizeLow;
  return true;
}

inline bool DecodeUtf8(const std::string &bytes, std::wstring &text) {
  text.clear();
  if (bytes.empty())
    return true;
  if (bytes.find('\0') != std::string::npos)
    return false;
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         bytes.data(),
                                         static_cast<int>(bytes.size()),
                                         nullptr, 0);
  if (length <= 0)
    return false;
  text.resize(length);
  return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
                             static_cast<int>(bytes.size()), text.data(),
                             length) == length;
}

inline bool EncodeUtf8(std::wstring_view text, std::string &bytes) {
  bytes.clear();
  if (text.empty())
    return true;
  if (text.find(L'\0') != std::wstring_view::npos)
    return false;
  if (text.size() > kMaxBytes)
    return false;
  const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                         text.data(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
  if (length <= 0 || length > static_cast<int>(kMaxBytes))
    return false;
  bytes.resize(length);
  return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                             static_cast<int>(text.size()), bytes.data(),
                             length, nullptr, nullptr) == length;
}

inline ReadResult Read(const std::filesystem::path &root) {
  if (!SafeDirectory(root))
    return {};
  const auto path = root / kFileName;
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    return {error == ERROR_FILE_NOT_FOUND ? ReadStatus::Missing
                                         : ReadStatus::Error, {}};
  }
  DWORD size = 0;
  std::string bytes;
  bool safe = SafeFileHandle(file, size);
  if (safe) {
    bytes.resize(size);
    DWORD actual = 0;
    safe = size == 0 || (ReadFile(file, bytes.data(), size, &actual, nullptr) &&
                         actual == size);
  }
  safe = CloseHandle(file) != FALSE && safe;
  std::wstring text;
  if (!safe || !SafeDirectory(root) || !DecodeUtf8(bytes, text))
    return {};
  return {ReadStatus::Ok, std::move(text)};
}

// The expected snapshot prevents a stale editor from replacing notes changed
// by another process. A failed read is never treated as an empty note.
inline bool Write(const std::filesystem::path &root, std::wstring_view text,
                  const ReadResult &expected,
                  const std::function<bool()> &revalidate = {}) {
  std::lock_guard<std::mutex> lock(mutex);
  std::string bytes;
  if (!SafeDirectory(root) || !EncodeUtf8(text, bytes) ||
      (revalidate && !revalidate()))
    return false;
  const ReadResult current = Read(root);
  if (current.status == ReadStatus::Error ||
      current.status != expected.status || current.text != expected.text)
    return false;

  const auto path = root / kFileName;
  for (unsigned attempt = 0; attempt < 16; ++attempt) {
    const auto stage = root /
        (L".ctSpaces-notes-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt));
    HANDLE file = CreateFileW(stage.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL |
                                              FILE_FLAG_WRITE_THROUGH,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      if (GetLastError() == ERROR_FILE_EXISTS ||
          GetLastError() == ERROR_ALREADY_EXISTS)
        continue;
      return false;
    }
    DWORD written = 0;
    const bool saved = (bytes.empty() ||
                        (WriteFile(file, bytes.data(),
                                   static_cast<DWORD>(bytes.size()), &written,
                                   nullptr) && written == bytes.size())) &&
                       FlushFileBuffers(file) != FALSE;
    const bool closed = CloseHandle(file) != FALSE;
    const bool ready = saved && closed && SafeDirectory(root) &&
                       (!revalidate || revalidate()) &&
                       [&]() {
                         const ReadResult latest = Read(root);
                         return latest.status == expected.status &&
                                latest.text == expected.text;
                       }();
    const bool moved = ready &&
        MoveFileExW(stage.c_str(), path.c_str(),
                    MOVEFILE_WRITE_THROUGH |
                    (expected.status == ReadStatus::Missing
                         ? 0 : MOVEFILE_REPLACE_EXISTING));
    if (!moved) {
      DeleteFileW(stage.c_str());
      return false;
    }
    return true;
  }
  return false;
}

// A document snapshot includes both formats. In particular, a legacy TXT
// change must invalidate an editor which is about to save its first RTF file.
struct FileSnapshot {
  ReadStatus status = ReadStatus::Error;
  std::string bytes;
};

struct DocumentResult {
  ReadStatus status = ReadStatus::Error;
  bool rich = false;
  std::string rtf;
  std::wstring text;
  FileSnapshot plain_snapshot;
  FileSnapshot rich_snapshot;
};

inline FileSnapshot ReadFileSnapshot(const std::filesystem::path &root,
                                     const wchar_t *name, DWORD maxBytes) {
  if (!SafeDirectory(root))
    return {};
  const auto path = root / name;
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return {GetLastError() == ERROR_FILE_NOT_FOUND ? ReadStatus::Missing
                                                  : ReadStatus::Error, {}};
  DWORD size = 0;
  std::string bytes;
  bool safe = SafeFileHandle(file, size, maxBytes);
  if (safe) {
    bytes.resize(size);
    DWORD actual = 0;
    safe = size == 0 || (ReadFile(file, bytes.data(), size, &actual, nullptr) &&
                         actual == size);
  }
  safe = CloseHandle(file) != FALSE && safe;
  if (!safe || !SafeDirectory(root))
    return {};
  return {ReadStatus::Ok, std::move(bytes)};
}

// Rich Edit's text-only RTF export is a small subset of RTF. Verify the
// structure before handing it to Rich Edit, and reject embedded objects,
// pictures, fields, external file references, and binary payloads.
inline bool ValidRichText(std::string_view rtf) {
  if (rtf.size() < 7 || rtf.size() > kMaxRichBytes ||
      rtf.substr(0, 6) != "{\\rtf1" ||
      rtf.find('\0') != std::string_view::npos)
    return false;
  unsigned depth = 0;
  bool ended = false;
  for (size_t i = 0; i < rtf.size();) {
    const unsigned char ch = static_cast<unsigned char>(rtf[i]);
    if (ended) {
      if (ch != ' ' && ch != '\r' && ch != '\n' && ch != '\t')
        return false;
      ++i;
    } else if (ch == '{') {
      if (++depth > 64)
        return false;
      ++i;
    } else if (ch == '}') {
      if (depth == 0)
        return false;
      ended = --depth == 0;
      ++i;
    } else if (ch == '\\') {
      if (++i == rtf.size())
        return false;
      const unsigned char next = static_cast<unsigned char>(rtf[i]);
      if (std::isalpha(next)) {
        const size_t start = i;
        while (i < rtf.size() &&
               std::isalpha(static_cast<unsigned char>(rtf[i])))
          ++i;
        std::string word(rtf.substr(start, i - start));
        for (char &letter : word)
          letter = static_cast<char>(
              std::tolower(static_cast<unsigned char>(letter)));
        if (word.size() > 64 || word == "bin" || word == "object" ||
            word == "pict" || word == "field" || word == "fldinst" ||
            word == "fldrslt" || word == "datafield" || word == "file" ||
            word == "filetbl" || word == "filename" ||
            word == "includegraphics" || word == "htmltag" ||
            word == "htmlrtf" || word == "do" ||
            word.starts_with("obj") || word.starts_with("shp") ||
            word.starts_with("blip"))
          return false;
        if (i < rtf.size() && rtf[i] == '-')
          ++i;
        while (i < rtf.size() &&
               std::isdigit(static_cast<unsigned char>(rtf[i])))
          ++i;
        if (i < rtf.size() && rtf[i] == ' ')
          ++i;
      } else if (next == '\'') {
        if (i + 2 >= rtf.size() ||
            !std::isxdigit(static_cast<unsigned char>(rtf[i + 1])) ||
            !std::isxdigit(static_cast<unsigned char>(rtf[i + 2])))
          return false;
        i += 3;
      } else if (next == '\\' || next == '{' || next == '}' ||
                 next == '~' || next == '-' || next == '_' || next == '*') {
        ++i;
      } else {
        return false;
      }
    } else {
      if (ch < 0x20 && ch != '\r' && ch != '\n' && ch != '\t')
        return false;
      ++i;
    }
  }
  return ended && depth == 0;
}

inline DocumentResult ReadDocument(const std::filesystem::path &root) {
  DocumentResult result;
  result.plain_snapshot = ReadFileSnapshot(root, kFileName, kMaxBytes);
  result.rich_snapshot = ReadFileSnapshot(root, kRichFileName, kMaxRichBytes);
  if (result.plain_snapshot.status == ReadStatus::Error ||
      result.rich_snapshot.status == ReadStatus::Error)
    return result;
  if (result.rich_snapshot.status == ReadStatus::Ok) {
    if (!ValidRichText(result.rich_snapshot.bytes))
      return result;
    result.status = ReadStatus::Ok;
    result.rich = true;
    result.rtf = result.rich_snapshot.bytes;
    return result;
  }
  if (result.plain_snapshot.status == ReadStatus::Missing) {
    result.status = ReadStatus::Missing;
    return result;
  }
  if (DecodeUtf8(result.plain_snapshot.bytes, result.text))
    result.status = ReadStatus::Ok;
  return result;
}

// RichEdit exports U+2003 as \emspace but imports that control as U+0020.
// Use an explicit Unicode group so checklist spacing survives a round trip.
// Tokenize escaped backslashes too: literal text such as "\\emspace" is data.
inline bool PreserveEditorSpacing(std::string &rtf) {
  std::string normalized;
  normalized.reserve(rtf.size());
  for (size_t i = 0; i < rtf.size();) {
    if (rtf[i] != '\\' || i + 1 == rtf.size()) {
      normalized.push_back(rtf[i++]);
    } else {
      const size_t start = i++;
      if (!std::isalpha(static_cast<unsigned char>(rtf[i]))) {
        normalized.append(rtf, start, 2);
        ++i;
      } else {
        const size_t word = i;
        while (i < rtf.size() && std::isalpha(static_cast<unsigned char>(rtf[i]))) ++i;
        if (rtf.compare(word, i - word, "emspace") == 0 &&
            (i == rtf.size() || (rtf[i] != '-' && !std::isdigit(static_cast<unsigned char>(rtf[i]))))) {
          if (i < rtf.size() && rtf[i] == ' ') ++i;
          normalized += "{\\uc1\\u8195?}";
        } else {
          normalized.append(rtf, start, i - start);
        }
      }
    }
    if (normalized.size() > kMaxRichBytes) return false;
  }
  rtf = std::move(normalized);
  return true;
}

// Use only for a native editor export. Store hyperlink display text without
// its field instruction; RichEdit detects the URL again on the next load.
// Incoming files must still pass ValidRichText without this normalization.
inline bool NormalizeEditorExport(std::string &rtf) {
  if (!rtf.empty() && rtf.back() == '\0')
    rtf.pop_back();
  const auto groupEnd = [&](size_t start) -> size_t {
    unsigned depth = 0;
    for (size_t i = start; i < rtf.size(); ++i) {
      if (rtf[i] == '\\') {
        if (++i == rtf.size()) return std::string::npos;
      } else if (rtf[i] == '{') {
        if (++depth > 64) return std::string::npos;
      } else if (rtf[i] == '}') {
        if (!depth) return std::string::npos;
        if (--depth == 0) return i + 1;
      }
    }
    return std::string::npos;
  };
  size_t cursor = 0;
  while ((cursor = rtf.find("{\\field", cursor)) != std::string::npos) {
    const size_t fieldEnd = groupEnd(cursor);
    const size_t instruction = cursor + 7;
    if (fieldEnd == std::string::npos ||
        rtf.compare(instruction, 11, "{\\*\\fldinst") != 0)
      return false;
    const size_t instructionEnd = groupEnd(instruction);
    if (instructionEnd == std::string::npos || instructionEnd >= fieldEnd ||
        rtf.compare(instructionEnd, 9, "{\\fldrslt") != 0)
      return false;
    const size_t resultEnd = groupEnd(instructionEnd);
    if (resultEnd != fieldEnd - 1)
      return false;
    const size_t content = instructionEnd + 9;
    std::string display = "{" + rtf.substr(content, resultEnd - 1 - content) + "}";
    rtf.replace(cursor, fieldEnd - cursor, display);
    // Examine the inserted display as well; any nested field must be flattened.
  }
  return PreserveEditorSpacing(rtf) && ValidRichText(rtf);
}

inline bool SameSnapshot(const FileSnapshot &a, const FileSnapshot &b) {
  return a.status == b.status && a.bytes == b.bytes;
}

inline bool WriteDocument(const std::filesystem::path &root,
                          std::string_view rtf, const DocumentResult &expected,
                          const std::function<bool()> &revalidate = {}) {
  std::lock_guard<std::mutex> lock(mutex);
  if (!SafeDirectory(root) || !ValidRichText(rtf) ||
      expected.status == ReadStatus::Error ||
      (revalidate && !revalidate()))
    return false;
  const auto matches = [&]() {
    const DocumentResult current = ReadDocument(root);
    return current.status != ReadStatus::Error &&
           SameSnapshot(current.plain_snapshot, expected.plain_snapshot) &&
           SameSnapshot(current.rich_snapshot, expected.rich_snapshot);
  };
  if (!matches())
    return false;

  const auto path = root / kRichFileName;
  for (unsigned attempt = 0; attempt < 16; ++attempt) {
    const auto stage = root /
        (L".ctSpaces-rich-notes-" + std::to_wstring(GetCurrentProcessId()) +
         L"-" + std::to_wstring(GetTickCount64()) + L"-" +
         std::to_wstring(attempt));
    HANDLE file = CreateFileW(stage.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL |
                                              FILE_FLAG_WRITE_THROUGH,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      if (GetLastError() == ERROR_FILE_EXISTS ||
          GetLastError() == ERROR_ALREADY_EXISTS)
        continue;
      return false;
    }
    DWORD written = 0;
    const bool saved = WriteFile(file, rtf.data(),
                                 static_cast<DWORD>(rtf.size()), &written,
                                 nullptr) && written == rtf.size() &&
                       FlushFileBuffers(file) != FALSE;
    const bool closed = CloseHandle(file) != FALSE;
    const bool ready = saved && closed && SafeDirectory(root) &&
                       (!revalidate || revalidate()) && matches();
    const bool moved = ready &&
        MoveFileExW(stage.c_str(), path.c_str(),
                    MOVEFILE_WRITE_THROUGH |
                    (expected.rich_snapshot.status == ReadStatus::Missing
                         ? 0 : MOVEFILE_REPLACE_EXISTING));
    if (!moved) {
      DeleteFileW(stage.c_str());
      return false;
    }
    return true;
  }
  return false;
}

struct NotePage {
  std::wstring name;
  bool rich = false; // False is used only for an unsaved legacy TXT page.
  std::string rtf;
  std::wstring text;
};

struct NotebookResult {
  ReadStatus status = ReadStatus::Error;
  std::vector<NotePage> pages;
  FileSnapshot plain_snapshot;
  FileSnapshot rich_snapshot;
  FileSnapshot notebook_snapshot;
};

inline bool NameSpace(wchar_t ch) {
  return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n' ||
         ch == 0x00a0 || ch == 0x1680 ||
         (ch >= 0x2000 && ch <= 0x200a) || ch == 0x2028 ||
         ch == 0x2029 || ch == 0x202f || ch == 0x205f ||
         ch == 0x3000;
}

inline bool ValidPageName(std::wstring_view name) {
  if (name.empty() || name.size() > 64 || NameSpace(name.front()) ||
      NameSpace(name.back()))
    return false;
  for (size_t i = 0; i < name.size(); ++i) {
    const wchar_t ch = name[i];
    if (ch < 0x20 || (ch >= 0x7f && ch <= 0x9f) ||
        ch == 0x00ad || ch == 0x061c || ch == 0x180e ||
        (ch >= 0x200b && ch <= 0x200f) ||
        (ch >= 0x2028 && ch <= 0x202e) ||
        (ch >= 0x2060 && ch <= 0x206f) || ch == 0xfeff ||
        (ch >= 0xfdd0 && ch <= 0xfdef) ||
        (ch >= 0xfff9 && ch <= 0xffff))
      return false;
    if (ch >= 0xd800 && ch <= 0xdbff) {
      if (++i == name.size() || name[i] < 0xdc00 || name[i] > 0xdfff)
        return false;
      const uint32_t codepoint = 0x10000 +
          (static_cast<uint32_t>(ch) - 0xd800) * 0x400 +
          (static_cast<uint32_t>(name[i]) - 0xdc00);
      if ((codepoint & 0xfffeu) == 0xfffeu)
        return false;
    } else if (ch >= 0xdc00 && ch <= 0xdfff) {
      return false;
    }
  }
  std::string encoded;
  return EncodeUtf8(name, encoded);
}

inline bool SamePageName(std::wstring_view a, std::wstring_view b) {
  return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                              static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

inline void AppendU32(std::string &bytes, uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8)
    bytes.push_back(static_cast<char>(value >> shift));
}

inline bool ReadU32(std::string_view bytes, size_t &offset, uint32_t &value) {
  if (offset > bytes.size() || bytes.size() - offset < 4)
    return false;
  value = 0;
  for (unsigned shift = 0; shift < 32; shift += 8)
    value |= static_cast<uint32_t>(
                 static_cast<unsigned char>(bytes[offset++])) << shift;
  return true;
}

// CTNBOOK1, a little-endian version and count, then length-prefixed UTF-8
// name and RTF for each page. Length checks precede all page allocations.
inline bool SerializeNotebook(const std::vector<NotePage> &pages,
                              std::string &bytes) {
  bytes.clear();
  if (pages.empty() || pages.size() > kMaxPages)
    return false;
  bytes.assign("CTNBOOK1", 8);
  AppendU32(bytes, 1);
  AppendU32(bytes, static_cast<uint32_t>(pages.size()));
  for (size_t i = 0; i < pages.size(); ++i) {
    const auto &page = pages[i];
    std::string name;
    if (!page.rich || !page.text.empty() || !ValidPageName(page.name) ||
        !EncodeUtf8(page.name, name) || !ValidRichText(page.rtf))
      return false;
    for (size_t j = 0; j < i; ++j)
      if (SamePageName(page.name, pages[j].name))
        return false;
    const size_t needed = 8 + name.size() + page.rtf.size();
    if (needed > kMaxNotebookBytes - bytes.size())
      return false;
    AppendU32(bytes, static_cast<uint32_t>(name.size()));
    AppendU32(bytes, static_cast<uint32_t>(page.rtf.size()));
    bytes.append(name);
    bytes.append(page.rtf);
  }
  return true;
}

inline bool ParseNotebook(std::string_view bytes,
                          std::vector<NotePage> &pages) {
  pages.clear();
  if (bytes.size() < 16 || bytes.size() > kMaxNotebookBytes ||
      bytes.substr(0, 8) != "CTNBOOK1")
    return false;
  size_t offset = 8;
  uint32_t version = 0, count = 0;
  if (!ReadU32(bytes, offset, version) || version != 1 ||
      !ReadU32(bytes, offset, count) || count == 0 || count > kMaxPages)
    return false;
  pages.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t nameSize = 0, rtfSize = 0;
    if (!ReadU32(bytes, offset, nameSize) ||
        !ReadU32(bytes, offset, rtfSize) || nameSize == 0 ||
        nameSize > 256 || rtfSize > kMaxRichBytes ||
        offset > bytes.size() ||
        static_cast<size_t>(nameSize) + rtfSize > bytes.size() - offset)
      return false;
    std::wstring name;
    if (!DecodeUtf8(std::string(bytes.substr(offset, nameSize)), name) ||
        !ValidPageName(name))
      return false;
    offset += nameSize;
    const std::string_view rtf = bytes.substr(offset, rtfSize);
    if (!ValidRichText(rtf))
      return false;
    offset += rtfSize;
    for (const auto &prior : pages)
      if (SamePageName(name, prior.name))
        return false;
    pages.push_back({std::move(name), true, std::string(rtf), {}});
  }
  return offset == bytes.size();
}

inline NotebookResult ReadNotebook(const std::filesystem::path &root) {
  NotebookResult result;
  result.plain_snapshot = ReadFileSnapshot(root, kFileName, kMaxBytes);
  result.rich_snapshot = ReadFileSnapshot(root, kRichFileName, kMaxRichBytes);
  result.notebook_snapshot =
      ReadFileSnapshot(root, kNotebookFileName, kMaxNotebookBytes);
  if (result.plain_snapshot.status == ReadStatus::Error ||
      result.rich_snapshot.status == ReadStatus::Error ||
      result.notebook_snapshot.status == ReadStatus::Error)
    return result;
  if (result.notebook_snapshot.status == ReadStatus::Ok) {
    if (ParseNotebook(result.notebook_snapshot.bytes, result.pages))
      result.status = ReadStatus::Ok;
    return result;
  }
  NotePage initial;
  initial.name = L"Notes";
  if (result.rich_snapshot.status == ReadStatus::Ok) {
    if (!ValidRichText(result.rich_snapshot.bytes))
      return result;
    initial.rich = true;
    initial.rtf = result.rich_snapshot.bytes;
    result.status = ReadStatus::Ok;
  } else if (result.plain_snapshot.status == ReadStatus::Ok) {
    if (!DecodeUtf8(result.plain_snapshot.bytes, initial.text))
      return result;
    result.status = ReadStatus::Ok;
  } else {
    result.status = ReadStatus::Missing;
  }
  result.pages.push_back(std::move(initial));
  return result;
}

inline bool WriteNotebook(const std::filesystem::path &root,
                          const std::vector<NotePage> &pages,
                          const NotebookResult &expected,
                          const std::function<bool()> &revalidate = {}) {
  std::lock_guard<std::mutex> lock(mutex);
  std::string bytes;
  if (!SafeDirectory(root) || !SerializeNotebook(pages, bytes) ||
      expected.status == ReadStatus::Error ||
      (revalidate && !revalidate()))
    return false;
  const auto matches = [&]() {
    const NotebookResult current = ReadNotebook(root);
    return current.status != ReadStatus::Error &&
           SameSnapshot(current.plain_snapshot, expected.plain_snapshot) &&
           SameSnapshot(current.rich_snapshot, expected.rich_snapshot) &&
           SameSnapshot(current.notebook_snapshot, expected.notebook_snapshot);
  };
  if (!matches())
    return false;
  const auto path = root / kNotebookFileName;
  for (unsigned attempt = 0; attempt < 16; ++attempt) {
    const auto stage = root /
        (L".ctSpaces-notebook-" + std::to_wstring(GetCurrentProcessId()) +
         L"-" + std::to_wstring(GetTickCount64()) + L"-" +
         std::to_wstring(attempt));
    HANDLE file = CreateFileW(stage.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL |
                                              FILE_FLAG_WRITE_THROUGH,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      if (GetLastError() == ERROR_FILE_EXISTS ||
          GetLastError() == ERROR_ALREADY_EXISTS)
        continue;
      return false;
    }
    DWORD written = 0;
    const bool saved = WriteFile(file, bytes.data(),
                                 static_cast<DWORD>(bytes.size()), &written,
                                 nullptr) && written == bytes.size() &&
                       FlushFileBuffers(file) != FALSE;
    const bool closed = CloseHandle(file) != FALSE;
    const bool ready = saved && closed && SafeDirectory(root) &&
                       (!revalidate || revalidate()) && matches();
    const bool moved = ready &&
        MoveFileExW(stage.c_str(), path.c_str(),
                    MOVEFILE_WRITE_THROUGH |
                    (expected.notebook_snapshot.status == ReadStatus::Missing
                         ? 0 : MOVEFILE_REPLACE_EXISTING));
    if (!moved) {
      DeleteFileW(stage.c_str());
      return false;
    }
    return true;
  }
  return false;
}
} // namespace client_notes
