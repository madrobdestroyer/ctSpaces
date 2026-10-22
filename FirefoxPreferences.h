#pragma once

#include <windows.h>
#include "ConfigPersistence.h"
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace firefox_preferences {
inline constexpr size_t kMaximumUserJsBytes = 4u * 1024u * 1024u;

namespace detail {
struct FileHandle {
  HANDLE value = INVALID_HANDLE_VALUE;
  ~FileHandle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
  void Close() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); value = INVALID_HANDLE_VALUE; }
};
inline bool ReadBoundedFile(HANDLE file, std::string &bytes) {
  BY_HANDLE_FILE_INFORMATION information{};
  LARGE_INTEGER size{};
  if (file == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file, &information) ||
      GetFileType(file) != FILE_TYPE_DISK ||
      (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
      !GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
      size.QuadPart > static_cast<LONGLONG>(kMaximumUserJsBytes)) return false;
  bytes.assign(static_cast<size_t>(size.QuadPart), '\0');
  DWORD read = 0;
  return bytes.empty() || (ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size());
}
inline bool Space(char c) { return c == ' ' || c == '\t' || c == '\v' || c == '\r' || c == '\n' || c == '\f'; }
inline bool Identifier(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_' || c == '$';
}
inline bool SkipTrivia(std::string_view text, size_t &at) {
  for (;;) {
    while (at < text.size() && Space(text[at])) ++at;
    // libpref supports Python-style line comments in user.js as well.
    if (at < text.size() && text[at] == '#') {
      while (at < text.size() && text[at] != '\n' && text[at] != '\r') ++at;
      continue;
    }
    if (at + 1 >= text.size() || text[at] != '/') return true;
    if (text[at + 1] == '/') {
      at += 2;
      while (at < text.size() && text[at] != '\n' && text[at] != '\r') ++at;
    } else if (text[at + 1] == '*') {
      const size_t end = text.find("*/", at + 2);
      if (end == std::string_view::npos) return false;
      at = end + 2;
    } else return true;
  }
}
inline int Hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
inline bool ReadString(std::string_view text, size_t &at, std::string &value) {
  value.clear();
  if (at >= text.size() || (text[at] != '"' && text[at] != '\'')) return false;
  const char quote = text[at++];
  while (at < text.size()) {
    char c = text[at++];
    if (c == quote) return true;
    // Mozilla's preference grammar permits literal newlines in quoted values.
    if (c == '\\') {
      if (at == text.size()) return false;
      c = text[at++];
      if (c == 'u' || c == 'x') {
        const size_t count = c == 'u' ? 4 : 2;
        if (at + count > text.size()) return false;
        unsigned code = 0;
        for (size_t i = 0; i < count; ++i) {
          const int digit = Hex(text[at++]);
          if (digit < 0) return false;
          code = (code << 4) | static_cast<unsigned>(digit);
        }
        // The managed key is ASCII; other string values are preserved verbatim.
        value.push_back(code < 128 ? static_cast<char>(code) : '\x7f');
        continue;
      }
      if (c == 'n') c = '\n';
      else if (c == 'r') c = '\r';
      else if (c == 't') c = '\t';
    }
    value.push_back(c);
  }
  return false;
}
} // namespace detail

// Scan actual preference calls, excluding comments and quoted examples. Replace
// every active occurrence so a later duplicate cannot undo the requested value.
inline bool UpdateStartupPreferenceText(std::string_view original, bool restore,
                                        std::string &updated) {
  struct Replacement { size_t start; size_t end; };
  std::vector<Replacement> replacements;
  size_t at = 0;
  while (at < original.size()) {
    if (!detail::SkipTrivia(original, at)) return false;
    if (at == original.size()) break;
    if (original[at] == '"' || original[at] == '\'') {
      std::string ignored;
      if (!detail::ReadString(original, at, ignored)) return false;
      continue;
    }
    if (!detail::Identifier(original[at])) { ++at; continue; }
    const size_t tokenStart = at;
    while (at < original.size() && detail::Identifier(original[at])) ++at;
    if (original.substr(tokenStart, at - tokenStart) != "user_pref") continue;
    size_t probe = at;
    if (!detail::SkipTrivia(original, probe)) return false;
    if (probe == original.size() || original[probe++] != '(') continue;
    if (!detail::SkipTrivia(original, probe)) return false;
    std::string key;
    if (!detail::ReadString(original, probe, key)) continue;
    if (key != "browser.startup.page") continue;
    if (!detail::SkipTrivia(original, probe) || probe == original.size() ||
        original[probe++] != ',' || !detail::SkipTrivia(original, probe)) return false;
    const size_t valueStart = probe;
    if (probe < original.size() && (original[probe] == '-' || original[probe] == '+')) ++probe;
    const size_t digitsStart = probe;
    while (probe < original.size() && original[probe] >= '0' && original[probe] <= '9') ++probe;
    if (probe == digitsStart) return false;
    const size_t valueEnd = probe;
    if (!detail::SkipTrivia(original, probe) || probe == original.size() || original[probe++] != ')') return false;
    replacements.push_back({valueStart, valueEnd});
    at = probe;
  }
  updated.assign(original);
  const char *value = restore ? "3" : "1";
  for (auto i = replacements.rbegin(); i != replacements.rend(); ++i)
    updated.replace(i->start, i->end - i->start, value);
  if (replacements.empty()) {
    const size_t firstLf = original.find('\n');
    const std::string_view newline = firstLf != std::string_view::npos &&
        (firstLf == 0 || original[firstLf - 1] != '\r') ? "\n" : "\r\n";
    if (!updated.empty() && updated.back() != '\n' && updated.back() != '\r') updated += newline;
    updated += "user_pref(\"browser.startup.page\", ";
    updated += value;
    updated += ");";
    updated += newline;
  }
  return true;
}

inline bool UpdateStartupPreferenceFile(const std::filesystem::path &profile,
                                       bool restore, std::wstring *errorDetails = nullptr) {
  if (errorDetails) errorDetails->clear();
  namespace fs = std::filesystem;
  try {
    const DWORD profileAttributes = GetFileAttributesW(profile.c_str());
    if (profileAttributes == INVALID_FILE_ATTRIBUTES ||
        !(profileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (profileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    const fs::path destination = profile / L"user.js";
    std::string original;
    detail::FileHandle input;
    input.value = CreateFileW(destination.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    const bool missing = input.value == INVALID_HANDLE_VALUE;
    if (!missing) {
      if (!detail::ReadBoundedFile(input.value, original)) return false;
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND) return false;
    input.Close();
    std::string updated;
    if (!UpdateStartupPreferenceText(original, restore, updated)) return false;
    // Never publish a file that this same helper would reject on the next
    // launch. Appending a missing preference can grow an otherwise valid input.
    if (updated.size() > kMaximumUserJsBytes) return false;
    return config_persistence::MutateRawFileAtomically(destination,
        [&](const fs::path &staged, std::wstring &) {
          detail::FileHandle stage;
          stage.value = CreateFileW(staged.c_str(), GENERIC_READ | GENERIC_WRITE,
              0, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
          std::string copied;
          if (!detail::ReadBoundedFile(stage.value, copied) || copied != original) return false;
          LARGE_INTEGER start{}; DWORD written = 0;
          return SetFilePointerEx(stage.value, start, nullptr, FILE_BEGIN) &&
                 SetEndOfFile(stage.value) &&
                 WriteFile(stage.value, updated.data(), static_cast<DWORD>(updated.size()), &written, nullptr) &&
                 written == updated.size();
        }, kMaximumUserJsBytes, missing, errorDetails);
  } catch (...) {
    return false;
  }
}

namespace detail {
inline constexpr std::string_view kTabLayoutMarker = "// ctSpaces tab-layout ";
inline constexpr std::string_view kTabLayoutBegin =
    "// ctSpaces tab-layout begin v1; separator=";
inline constexpr std::string_view kTabLayoutEnd = "// ctSpaces tab-layout end v1";
struct TabLayoutBlock {
  bool found = false;
  size_t begin = 0;
  size_t eraseBegin = 0;
  size_t end = 0;
};
inline std::string TabLayoutBody(bool vertical, std::string_view newline) {
  std::string body;
  if (vertical) {
    body = "user_pref(\"sidebar.revamp\", true);";
    body += newline;
  }
  body += vertical ? "user_pref(\"sidebar.verticalTabs\", true);"
                   : "user_pref(\"sidebar.verticalTabs\", false);";
  body += newline;
  return body;
}

// A marker belongs to us only as a standalone line-comment token, never as
// text inside a string, another line comment, or a block comment. Only the
// exact generated body is removable: user edits inside it must fail closed.
inline bool FindTabLayoutBlock(std::string_view text, TabLayoutBlock &block) {
  block = {};
  bool open = false;
  size_t bodyBegin = 0;
  std::string_view newline;
  size_t at = 0;
  while (at < text.size()) {
    if (text[at] == '"' || text[at] == '\'') {
      std::string ignored;
      if (!ReadString(text, at, ignored)) return false;
      continue;
    }
    if (text[at] == '#') {
      while (at < text.size() && text[at] != '\r' && text[at] != '\n') ++at;
      continue;
    }
    if (at + 1 < text.size() && text.substr(at, 2) == "/*") {
      const size_t end = text.find("*/", at + 2);
      if (end == std::string_view::npos) return false;
      at = end + 2;
      continue;
    }
    if (at + 1 >= text.size() || text.substr(at, 2) != "//") {
      ++at;
      continue;
    }
    const size_t lineEnd = text.find_first_of("\r\n", at);
    const size_t contentEnd = lineEnd == std::string_view::npos ? text.size() : lineEnd;
    size_t afterLine = contentEnd;
    if (afterLine < text.size() && text[afterLine] == '\r') ++afterLine;
    if (afterLine < text.size() && text[afterLine] == '\n') ++afterLine;
    size_t lineBegin = at;
    while (lineBegin > 0 && text[lineBegin - 1] != '\r' && text[lineBegin - 1] != '\n') --lineBegin;
    bool standalone = true;
    for (size_t p = lineBegin; p < at; ++p) {
      if (text[p] != ' ' && text[p] != '\t') standalone = false;
    }
    const auto comment = text.substr(at, contentEnd - at);
    if (standalone && comment.starts_with(kTabLayoutMarker)) {
      // Our writer never indents a marker. Treat an indented or unknown
      // version as ambiguous rather than guessing ownership of user bytes.
      if (at != lineBegin) return false;
      if (comment.starts_with(kTabLayoutBegin)) {
        if (open || block.found || afterLine == contentEnd) return false;
        const auto separator = comment.substr(kTabLayoutBegin.size());
        size_t eraseBegin = at;
        if (separator == "crlf") {
          if (at < 2 || text.substr(at - 2, 2) != "\r\n") return false;
          eraseBegin -= 2;
        } else if (separator == "lf") {
          if (at == 0 || text[at - 1] != '\n' || (at > 1 && text[at - 2] == '\r')) return false;
          --eraseBegin;
        } else if (separator != "none") {
          return false;
        }
        block.begin = at;
        block.eraseBegin = eraseBegin;
        bodyBegin = afterLine;
        newline = text.substr(contentEnd, afterLine - contentEnd);
        open = true;
      } else if (comment == kTabLayoutEnd) {
        if (!open) return false;
        const auto body = text.substr(bodyBegin, at - bodyBegin);
        if (body != TabLayoutBody(true, newline) && body != TabLayoutBody(false, newline)) return false;
        block.end = afterLine;
        block.found = true;
        open = false;
      } else {
        return false;
      }
    }
    at = afterLine;
  }
  return !open;
}
} // namespace detail

// Preserve all user-owned preference rows, including duplicates. An explicit
// mode is appended last so Firefox's normal last-entry-wins behavior applies.
// nullopt releases only this managed block; it does not guess an old mode.
inline bool UpdateTabLayoutPreferenceText(std::string_view original,
                                         std::optional<bool> vertical,
                                         std::string &updated) {
  if (original.size() > kMaximumUserJsBytes) return false;
  detail::TabLayoutBlock block;
  if (!detail::FindTabLayoutBlock(original, block)) return false;
  std::string base(original);
  if (block.found) {
    size_t eraseBegin = block.eraseBegin;
    // If a user appended another preference after a block that originally
    // supplied a missing newline, retain that separator to avoid joining the
    // user's preceding comment/token to the newly added row.
    if (eraseBegin != block.begin && block.end < original.size() && eraseBegin > 0 &&
        original[eraseBegin - 1] != '\r' && original[eraseBegin - 1] != '\n' &&
        original[block.end] != '\r' && original[block.end] != '\n') {
      eraseBegin = block.begin;
    }
    base.erase(eraseBegin, block.end - eraseBegin);
  }
  if (!vertical.has_value()) {
    updated = std::move(base);
    return true;
  }
  const size_t firstLf = base.find('\n');
  const std::string_view newline = firstLf != std::string::npos &&
      (firstLf == 0 || base[firstLf - 1] != '\r') ? "\n" : "\r\n";
  const bool separator = !base.empty() && base.back() != '\r' && base.back() != '\n';
  std::string candidate = std::move(base);
  if (separator) candidate += newline;
  candidate += detail::kTabLayoutBegin;
  candidate += separator ? (newline == "\n" ? "lf" : "crlf") : "none";
  candidate += newline;
  candidate += detail::TabLayoutBody(*vertical, newline);
  candidate += detail::kTabLayoutEnd;
  candidate += newline;
  if (candidate.size() > kMaximumUserJsBytes) return false;
  updated = std::move(candidate);
  return true;
}

inline bool UpdateTabLayoutPreferenceFile(const std::filesystem::path &profile,
                                         std::optional<bool> vertical,
                                         std::wstring *errorDetails = nullptr) {
  if (errorDetails) errorDetails->clear();
  namespace fs = std::filesystem;
  try {
    const DWORD attributes = GetFileAttributesW(profile.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    const fs::path destination = profile / L"user.js";
    std::string original;
    detail::FileHandle input;
    input.value = CreateFileW(destination.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    const bool missing = input.value == INVALID_HANDLE_VALUE;
    if (!missing) {
      if (!detail::ReadBoundedFile(input.value, original)) return false;
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND) {
      return false;
    }
    input.Close();
    if (missing && !vertical.has_value()) return true;
    std::string updated;
    if (!UpdateTabLayoutPreferenceText(original, vertical, updated)) {
      if (errorDetails) *errorDetails = L"The Firefox tab-layout block is ambiguous, malformed or too large. Existing preferences were kept.";
      return false;
    }
    if (!missing && updated == original) return true;
    return config_persistence::MutateRawFileAtomically(destination,
        [&](const fs::path &staged, std::wstring &) {
          detail::FileHandle stage;
          stage.value = CreateFileW(staged.c_str(), GENERIC_READ | GENERIC_WRITE,
              0, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
          std::string copied;
          if (!detail::ReadBoundedFile(stage.value, copied) || copied != original) return false;
          LARGE_INTEGER start{};
          DWORD written = 0;
          return SetFilePointerEx(stage.value, start, nullptr, FILE_BEGIN) &&
                 SetEndOfFile(stage.value) &&
                 WriteFile(stage.value, updated.data(), static_cast<DWORD>(updated.size()), &written, nullptr) &&
                 written == updated.size();
        }, kMaximumUserJsBytes, missing && vertical.has_value(), errorDetails);
  } catch (...) {
    return false;
  }
}
} // namespace firefox_preferences
