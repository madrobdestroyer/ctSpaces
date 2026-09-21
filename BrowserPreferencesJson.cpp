#include "BrowserPreferencesJson.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace browser_preferences {
namespace {

constexpr unsigned kMaximumJsonDepth = 256;

struct Span {
  size_t start = 0;
  size_t end = 0;
  unsigned occurrences = 0;
};

struct ObjectSpan {
  size_t open = 0;
  size_t close = 0;
  unsigned occurrences = 0;
  bool isObject = false;
  bool empty = false;
};

struct Targets {
  ObjectSpan root;
  ObjectSpan session;
  ObjectSpan profile;
  Span restoreOnStartup;
  Span startupUrls;
  Span urlsToRestoreOnStartup;
  Span exitType;
  Span exitedCleanly;
  bool unsafe = false;
};

enum class Scope { other, root, session, profile };

class JsonScanner {
public:
  explicit JsonScanner(std::string_view text) : text_(text) {}

  bool Scan(Targets &targets) {
    targets_ = &targets;
    if (text_.size() >= 3 &&
        static_cast<unsigned char>(text_[0]) == 0xEF &&
        static_cast<unsigned char>(text_[1]) == 0xBB &&
        static_cast<unsigned char>(text_[2]) == 0xBF) {
      position_ = 3;
    }

    SkipWhitespace();
    if (position_ >= text_.size() || text_[position_] != '{')
      return false;
    if (!ParseObject(0, Scope::root, &targets.root))
      return false;
    SkipWhitespace();
    return position_ == text_.size();
  }

private:
  bool ParseValue(unsigned depth, Scope objectScope = Scope::other,
                  ObjectSpan *objectSpan = nullptr) {
    if (depth > kMaximumJsonDepth || position_ >= text_.size())
      return false;

    switch (text_[position_]) {
    case '{':
      return ParseObject(depth, objectScope, objectSpan);
    case '[':
      return ParseArray(depth);
    case '"':
      return ParseString(nullptr);
    case 't':
      return ConsumeLiteral("true");
    case 'f':
      return ConsumeLiteral("false");
    case 'n':
      return ConsumeLiteral("null");
    default:
      return ParseNumber();
    }
  }

  bool ParseObject(unsigned depth, Scope scope, ObjectSpan *objectSpan) {
    if (depth > kMaximumJsonDepth || position_ >= text_.size() ||
        text_[position_] != '{') {
      return false;
    }

    const size_t open = position_++;
    SkipWhitespace();
    if (position_ < text_.size() && text_[position_] == '}') {
      if (objectSpan) {
        objectSpan->open = open;
        objectSpan->close = position_;
        objectSpan->isObject = true;
        objectSpan->empty = true;
      }
      ++position_;
      return true;
    }

    while (position_ < text_.size()) {
      std::string key;
      if (!ParseString(&key))
        return false;
      SkipWhitespace();
      if (position_ >= text_.size() || text_[position_++] != ':')
        return false;
      SkipWhitespace();
      if (position_ >= text_.size())
        return false;

      const size_t valueStart = position_;
      Scope childScope = Scope::other;
      ObjectSpan *childObject = nullptr;
      Span *targetValue = nullptr;

      if (scope == Scope::root && key == "session") {
        childScope = Scope::session;
        childObject = &targets_->session;
        if (++targets_->session.occurrences != 1)
          targets_->unsafe = true;
      } else if (scope == Scope::root && key == "profile") {
        childScope = Scope::profile;
        childObject = &targets_->profile;
        if (++targets_->profile.occurrences != 1)
          targets_->unsafe = true;
      } else if (scope == Scope::session) {
        if (key == "restore_on_startup")
          targetValue = &targets_->restoreOnStartup;
        else if (key == "startup_urls")
          targetValue = &targets_->startupUrls;
        else if (key == "urls_to_restore_on_startup")
          targetValue = &targets_->urlsToRestoreOnStartup;
      } else if (scope == Scope::profile) {
        if (key == "exit_type")
          targetValue = &targets_->exitType;
        else if (key == "exited_cleanly")
          targetValue = &targets_->exitedCleanly;
      }

      if (targetValue && ++targetValue->occurrences != 1)
        targets_->unsafe = true;

      if (!ParseValue(depth + 1, childScope, childObject))
        return false;

      if (targetValue) {
        targetValue->start = valueStart;
        targetValue->end = position_;
      }
      if (childObject && !childObject->isObject)
        targets_->unsafe = true;

      SkipWhitespace();
      if (position_ >= text_.size())
        return false;
      if (text_[position_] == '}') {
        if (objectSpan) {
          objectSpan->open = open;
          objectSpan->close = position_;
          objectSpan->isObject = true;
          objectSpan->empty = false;
        }
        ++position_;
        return true;
      }
      if (text_[position_++] != ',')
        return false;
      SkipWhitespace();
    }
    return false;
  }

  bool ParseArray(unsigned depth) {
    if (depth > kMaximumJsonDepth || position_ >= text_.size() ||
        text_[position_++] != '[') {
      return false;
    }
    SkipWhitespace();
    if (position_ < text_.size() && text_[position_] == ']') {
      ++position_;
      return true;
    }

    while (position_ < text_.size()) {
      if (!ParseValue(depth + 1))
        return false;
      SkipWhitespace();
      if (position_ >= text_.size())
        return false;
      if (text_[position_] == ']') {
        ++position_;
        return true;
      }
      if (text_[position_++] != ',')
        return false;
      SkipWhitespace();
    }
    return false;
  }

  bool ParseString(std::string *decoded) {
    if (position_ >= text_.size() || text_[position_++] != '"')
      return false;
    if (decoded)
      decoded->clear();

    while (position_ < text_.size()) {
      const unsigned char byte =
          static_cast<unsigned char>(text_[position_++]);
      if (byte == '"')
        return true;
      if (byte < 0x20)
        return false;

      if (byte == '\\') {
        if (position_ >= text_.size())
          return false;
        const char escape = text_[position_++];
        switch (escape) {
        case '"':
        case '\\':
        case '/':
          if (decoded)
            decoded->push_back(escape);
          break;
        case 'b':
          if (decoded)
            decoded->push_back('\b');
          break;
        case 'f':
          if (decoded)
            decoded->push_back('\f');
          break;
        case 'n':
          if (decoded)
            decoded->push_back('\n');
          break;
        case 'r':
          if (decoded)
            decoded->push_back('\r');
          break;
        case 't':
          if (decoded)
            decoded->push_back('\t');
          break;
        case 'u': {
          uint32_t codePoint = 0;
          if (!ParseHex4(codePoint))
            return false;
          if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
            if (position_ + 2 > text_.size() || text_[position_] != '\\' ||
                text_[position_ + 1] != 'u') {
              return false;
            }
            position_ += 2;
            uint32_t low = 0;
            if (!ParseHex4(low) || low < 0xDC00 || low > 0xDFFF)
              return false;
            codePoint = 0x10000 + ((codePoint - 0xD800) << 10) +
                        (low - 0xDC00);
          } else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
            return false;
          }
          if (decoded)
            AppendUtf8(codePoint, *decoded);
          break;
        }
        default:
          return false;
        }
        continue;
      }

      if (byte < 0x80) {
        if (decoded)
          decoded->push_back(static_cast<char>(byte));
        continue;
      }

      const size_t sequenceStart = position_ - 1;
      size_t continuationCount = 0;
      uint32_t codePoint = 0;
      uint32_t minimum = 0;
      if ((byte & 0xE0) == 0xC0) {
        continuationCount = 1;
        codePoint = byte & 0x1F;
        minimum = 0x80;
      } else if ((byte & 0xF0) == 0xE0) {
        continuationCount = 2;
        codePoint = byte & 0x0F;
        minimum = 0x800;
      } else if ((byte & 0xF8) == 0xF0) {
        continuationCount = 3;
        codePoint = byte & 0x07;
        minimum = 0x10000;
      } else {
        return false;
      }
      if (position_ + continuationCount > text_.size())
        return false;
      for (size_t index = 0; index < continuationCount; ++index) {
        const unsigned char continuation =
            static_cast<unsigned char>(text_[position_++]);
        if ((continuation & 0xC0) != 0x80)
          return false;
        codePoint = (codePoint << 6) | (continuation & 0x3F);
      }
      if (codePoint < minimum || codePoint > 0x10FFFF ||
          (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
        return false;
      }
      if (decoded) {
        decoded->append(text_.substr(sequenceStart, continuationCount + 1));
      }
    }
    return false;
  }

  bool ParseHex4(uint32_t &value) {
    if (position_ + 4 > text_.size())
      return false;
    value = 0;
    for (unsigned index = 0; index < 4; ++index) {
      const char digit = text_[position_++];
      value <<= 4;
      if (digit >= '0' && digit <= '9')
        value |= static_cast<uint32_t>(digit - '0');
      else if (digit >= 'a' && digit <= 'f')
        value |= static_cast<uint32_t>(digit - 'a' + 10);
      else if (digit >= 'A' && digit <= 'F')
        value |= static_cast<uint32_t>(digit - 'A' + 10);
      else
        return false;
    }
    return true;
  }

  static void AppendUtf8(uint32_t codePoint, std::string &output) {
    if (codePoint <= 0x7F) {
      output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FF) {
      output.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
      output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint <= 0xFFFF) {
      output.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
      output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
      output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
      output.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
      output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
      output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
      output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
  }

  bool ParseNumber() {
    const size_t start = position_;
    if (position_ < text_.size() && text_[position_] == '-')
      ++position_;
    if (position_ >= text_.size())
      return false;

    if (text_[position_] == '0') {
      ++position_;
      if (position_ < text_.size() && text_[position_] >= '0' &&
          text_[position_] <= '9') {
        return false;
      }
    } else if (text_[position_] >= '1' && text_[position_] <= '9') {
      do {
        ++position_;
      } while (position_ < text_.size() && text_[position_] >= '0' &&
               text_[position_] <= '9');
    } else {
      return false;
    }

    if (position_ < text_.size() && text_[position_] == '.') {
      ++position_;
      const size_t fractionalStart = position_;
      while (position_ < text_.size() && text_[position_] >= '0' &&
             text_[position_] <= '9') {
        ++position_;
      }
      if (position_ == fractionalStart)
        return false;
    }

    if (position_ < text_.size() &&
        (text_[position_] == 'e' || text_[position_] == 'E')) {
      ++position_;
      if (position_ < text_.size() &&
          (text_[position_] == '+' || text_[position_] == '-')) {
        ++position_;
      }
      const size_t exponentStart = position_;
      while (position_ < text_.size() && text_[position_] >= '0' &&
             text_[position_] <= '9') {
        ++position_;
      }
      if (position_ == exponentStart)
        return false;
    }
    return position_ > start;
  }

  bool ConsumeLiteral(std::string_view literal) {
    if (text_.substr(position_, literal.size()) != literal)
      return false;
    position_ += literal.size();
    return true;
  }

  void SkipWhitespace() {
    while (position_ < text_.size()) {
      const char value = text_[position_];
      if (value != ' ' && value != '\t' && value != '\r' && value != '\n')
        break;
      ++position_;
    }
  }

  std::string_view text_;
  size_t position_ = 0;
  Targets *targets_ = nullptr;
};

struct Edit {
  size_t start;
  size_t end;
  std::string replacement;
};

class ScopedHandle {
public:
  explicit ScopedHandle(HANDLE handle = INVALID_HANDLE_VALUE)
      : handle_(handle) {}
  ~ScopedHandle() {
    if (handle_ != INVALID_HANDLE_VALUE)
      CloseHandle(handle_);
  }
  ScopedHandle(const ScopedHandle &) = delete;
  ScopedHandle &operator=(const ScopedHandle &) = delete;
  HANDLE get() const { return handle_; }

private:
  HANDLE handle_;
};

void AddReplacement(std::string_view input, const Span &span,
                    std::string replacement, std::vector<Edit> &edits) {
  if (span.occurrences != 1)
    return;
  if (input.substr(span.start, span.end - span.start) == replacement)
    return;
  edits.push_back({span.start, span.end, std::move(replacement)});
}

bool WriteAll(HANDLE file, std::string_view text) {
  size_t writtenTotal = 0;
  while (writtenTotal < text.size()) {
    const size_t remaining = text.size() - writtenTotal;
    const DWORD requested = static_cast<DWORD>(
        (std::min)(remaining,
                   static_cast<size_t>((std::numeric_limits<DWORD>::max)())));
    DWORD written = 0;
    if (!WriteFile(file, text.data() + writtenTotal, requested, &written,
                   nullptr) ||
        written == 0) {
      return false;
    }
    writtenTotal += written;
  }
  return true;
}

bool ReadAll(HANDLE file, std::string &text) {
  size_t readTotal = 0;
  while (readTotal < text.size()) {
    const size_t remaining = text.size() - readTotal;
    const DWORD requested = static_cast<DWORD>(
        (std::min)(remaining,
                   static_cast<size_t>((std::numeric_limits<DWORD>::max)())));
    DWORD bytesRead = 0;
    if (!ReadFile(file, text.data() + readTotal, requested, &bytesRead,
                  nullptr) ||
        bytesRead == 0) {
      return false;
    }
    readTotal += bytesRead;
  }
  return true;
}

std::filesystem::path CreateStagingPath(const std::filesystem::path &path,
                                        HANDLE &handle) {
  static std::atomic_uint64_t sequence{0};
  for (unsigned attempt = 0; attempt < 32; ++attempt) {
    std::filesystem::path staged = path;
    staged += L".ctspaces-startup." + std::to_wstring(GetCurrentProcessId()) +
              L"." + std::to_wstring(++sequence) + L".tmp";
    handle = CreateFileW(staged.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                         FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (handle != INVALID_HANDLE_VALUE)
      return staged;
    if (GetLastError() != ERROR_FILE_EXISTS &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
      break;
    }
  }
  return {};
}

} // namespace

JsonEditResult EditStartupPreferencesJson(std::string_view input,
                                          bool restoreTabs,
                                          bool ensureSessionPreference,
                                          std::string &output) noexcept {
  try {
    output.assign(input);
    if (input.size() > kMaximumPreferencesJsonBytes)
      return JsonEditResult::unsafe_target;
    Targets targets;
    JsonScanner scanner(input);
    if (!scanner.Scan(targets))
      return JsonEditResult::invalid_json;
    if (targets.unsafe)
      return JsonEditResult::unsafe_target;

    std::vector<Edit> edits;
    const std::string restoreValue = restoreTabs ? "1" : "5";

    if (targets.session.occurrences == 1) {
      AddReplacement(input, targets.restoreOnStartup, restoreValue, edits);
      if (targets.restoreOnStartup.occurrences == 0 &&
          ensureSessionPreference) {
        edits.push_back(
            {targets.session.open + 1, targets.session.open + 1,
             "\"restore_on_startup\":" + restoreValue +
                 (targets.session.empty ? "" : ",")});
      }
    } else if (ensureSessionPreference) {
      edits.push_back(
          {targets.root.open + 1, targets.root.open + 1,
           "\"session\":{\"restore_on_startup\":" + restoreValue + "}" +
               (targets.root.empty ? "" : ",")});
    }

    if (!restoreTabs) {
      AddReplacement(input, targets.startupUrls, "[]", edits);
      AddReplacement(input, targets.urlsToRestoreOnStartup, "[]", edits);
    }
    AddReplacement(input, targets.exitType, "\"Normal\"", edits);
    AddReplacement(input, targets.exitedCleanly, "true", edits);

    if (edits.empty())
      return JsonEditResult::unchanged;

    std::sort(edits.begin(), edits.end(), [](const Edit &left,
                                             const Edit &right) {
      return left.start > right.start;
    });
    for (const Edit &edit : edits)
      output.replace(edit.start, edit.end - edit.start, edit.replacement);

    Targets validationTargets;
    JsonScanner validationScanner(output);
    if (!validationScanner.Scan(validationTargets) || validationTargets.unsafe) {
      output.assign(input);
      return JsonEditResult::invalid_json;
    }
    return JsonEditResult::updated;
  } catch (...) {
    output.assign(input);
    return JsonEditResult::invalid_json;
  }
}

FileUpdateResult UpdateStartupPreferencesFile(
    const std::filesystem::path &path, bool restoreTabs,
    bool ensureSessionPreference) noexcept {
  try {
    ScopedHandle inputHandle(CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr));
    if (inputHandle.get() == INVALID_HANDLE_VALUE) {
      const DWORD openError = GetLastError();
      if (openError == ERROR_FILE_NOT_FOUND || openError == ERROR_PATH_NOT_FOUND)
        return FileUpdateResult::missing;
      return FileUpdateResult::io_error;
    }

    BY_HANDLE_FILE_INFORMATION fileInformation{};
    if (!GetFileInformationByHandle(inputHandle.get(), &fileInformation))
      return FileUpdateResult::io_error;
    if (GetFileType(inputHandle.get()) != FILE_TYPE_DISK ||
        (fileInformation.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
      return FileUpdateResult::unsafe_target;
    }

    ULARGE_INTEGER fileSize{};
    fileSize.LowPart = fileInformation.nFileSizeLow;
    fileSize.HighPart = fileInformation.nFileSizeHigh;
    if (fileSize.QuadPart > kMaximumPreferencesJsonBytes)
      return FileUpdateResult::unsafe_target;

    std::string input(static_cast<size_t>(fileSize.QuadPart), '\0');
    if (!ReadAll(inputHandle.get(), input))
      return FileUpdateResult::io_error;

    std::string output;
    const JsonEditResult editResult = EditStartupPreferencesJson(
        input, restoreTabs, ensureSessionPreference, output);
    switch (editResult) {
    case JsonEditResult::unchanged:
      return FileUpdateResult::unchanged;
    case JsonEditResult::invalid_json:
      return FileUpdateResult::invalid_json;
    case JsonEditResult::unsafe_target:
      return FileUpdateResult::unsafe_target;
    case JsonEditResult::updated:
      break;
    }

    HANDLE stagedHandle = INVALID_HANDLE_VALUE;
    const std::filesystem::path stagedPath =
        CreateStagingPath(path, stagedHandle);
    if (stagedPath.empty() || stagedHandle == INVALID_HANDLE_VALUE)
      return FileUpdateResult::io_error;

    const bool writeSucceeded = WriteAll(stagedHandle, output) &&
                                FlushFileBuffers(stagedHandle) != FALSE;
    const bool closeSucceeded = CloseHandle(stagedHandle) != FALSE;
    if (!writeSucceeded || !closeSucceeded) {
      DeleteFileW(stagedPath.c_str());
      return FileUpdateResult::io_error;
    }

    if (!ReplaceFileW(path.c_str(), stagedPath.c_str(), nullptr, 0, nullptr,
                      nullptr)) {
      DeleteFileW(stagedPath.c_str());
      return FileUpdateResult::io_error;
    }
    return FileUpdateResult::updated;
  } catch (...) {
    return FileUpdateResult::io_error;
  }
}

} // namespace browser_preferences
