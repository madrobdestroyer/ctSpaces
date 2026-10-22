#include "BrowserPreferencesJson.h"
#include "ConfigPersistence.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#pragma comment(lib, "Ole32.lib")

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

struct TabLayoutTargets {
  std::array<std::string_view, 3> keys{};
  size_t length = 0;
  // The root plus each ancestor of the boolean leaf.
  std::array<ObjectSpan, 3> objects{};
  Span value;
  bool unsafe = false;
};
constexpr size_t kOutsideTabLayout = (std::numeric_limits<size_t>::max)();

bool SetTabLayoutPath(TabLayoutBrowser browser, TabLayoutTargets &targets) {
  switch (browser) {
  case TabLayoutBrowser::Edge:
    targets.keys = {"edge", "vertical_tabs", "opened"};
    targets.length = 3;
    return true;
  case TabLayoutBrowser::Chrome:
    targets.keys = {"vertical_tabs", "enabled", {}};
    targets.length = 2;
    return true;
  case TabLayoutBrowser::Brave:
    targets.keys = {"brave", "tabs", "vertical_tabs_enabled"};
    targets.length = 3;
    return true;
  }
  return false;
}

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
    if (!ParseObject(0, Scope::root, &targets.root,
                     layout_ ? 0 : kOutsideTabLayout))
      return false;
    SkipWhitespace();
    return position_ == text_.size();
  }

  bool ScanTabLayout(TabLayoutTargets &layout) {
    layout_ = &layout;
    Targets unusedStartupTargets;
    const bool valid = Scan(unusedStartupTargets);
    layout.objects[0] = unusedStartupTargets.root;
    return valid;
  }

private:
  bool ParseValue(unsigned depth, Scope objectScope = Scope::other,
                  ObjectSpan *objectSpan = nullptr,
                  size_t layoutLevel = kOutsideTabLayout) {
    if (depth > kMaximumJsonDepth || position_ >= text_.size())
      return false;

    switch (text_[position_]) {
    case '{':
      return ParseObject(depth, objectScope, objectSpan, layoutLevel);
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

  bool ParseObject(unsigned depth, Scope scope, ObjectSpan *objectSpan,
                   size_t layoutLevel = kOutsideTabLayout) {
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
      Span *layoutValue = nullptr;
      ObjectSpan *layoutObject = nullptr;
      size_t childLayoutLevel = kOutsideTabLayout;

      if (layout_ && layoutLevel < layout_->length &&
          key == layout_->keys[layoutLevel]) {
        if (layoutLevel + 1 == layout_->length) {
          layoutValue = &layout_->value;
          if (++layoutValue->occurrences != 1) layout_->unsafe = true;
        } else {
          layoutObject = &layout_->objects[layoutLevel + 1];
          if (++layoutObject->occurrences != 1) layout_->unsafe = true;
          childLayoutLevel = layoutLevel + 1;
        }
      }

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

      if (!ParseValue(depth + 1, childScope,
                      layoutObject ? layoutObject : childObject,
                      childLayoutLevel))
        return false;

      if (targetValue) {
        targetValue->start = valueStart;
        targetValue->end = position_;
      }
      if (childObject && !childObject->isObject)
        targets_->unsafe = true;
      if (layoutValue) {
        layoutValue->start = valueStart;
        layoutValue->end = position_;
      }
      if (layoutObject && !layoutObject->isObject) layout_->unsafe = true;

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
  TabLayoutTargets *layout_ = nullptr;
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
  void close() { if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; }

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

    // A successful update must remain readable by this same helper on the
    // next launch, including when a missing startup preference is inserted.
    if (output.size() > kMaximumPreferencesJsonBytes) {
      output.assign(input);
      return JsonEditResult::unsafe_target;
    }

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

JsonEditResult EditTabLayoutPreferencesJson(
    std::string_view input, TabLayoutBrowser browser, bool vertical,
    std::string &output) noexcept {
  try {
    TabLayoutTargets targets;
    if (input.size() > kMaximumPreferencesJsonBytes ||
        !SetTabLayoutPath(browser, targets)) {
      output.assign(input);
      return JsonEditResult::unsafe_target;
    }
    JsonScanner scanner(input);
    if (!scanner.ScanTabLayout(targets)) {
      output.assign(input);
      return JsonEditResult::invalid_json;
    }
    if (targets.unsafe) {
      output.assign(input);
      return JsonEditResult::unsafe_target;
    }
    const std::string_view desired = vertical ? "true" : "false";
    std::string candidate(input);
    if (targets.value.occurrences) {
      const auto old = input.substr(targets.value.start,
                                     targets.value.end - targets.value.start);
      if (old != "true" && old != "false") {
        output.assign(input);
        return JsonEditResult::unsafe_target;
      }
      if (old == desired) {
        output.swap(candidate);
        return JsonEditResult::unchanged;
      }
      candidate.replace(targets.value.start,
                          targets.value.end - targets.value.start, desired);
    } else {
      size_t parent = 0;
      while (parent + 1 < targets.length &&
             targets.objects[parent + 1].occurrences) ++parent;
      std::string inserted(desired);
      for (size_t key = targets.length; key-- > parent;) {
        inserted = "\"" + std::string(targets.keys[key]) + "\":" + inserted;
        if (key != parent) inserted = "{" + inserted + "}";
      }
      if (!targets.objects[parent].empty) inserted += ",";
      candidate.insert(targets.objects[parent].open + 1, inserted);
    }
    if (candidate.size() > kMaximumPreferencesJsonBytes) {
      output.assign(input);
      return JsonEditResult::unsafe_target;
    }
    TabLayoutTargets verified;
    SetTabLayoutPath(browser, verified);
    JsonScanner validation(candidate);
    if (!validation.ScanTabLayout(verified) || verified.unsafe ||
        verified.value.occurrences != 1 ||
        std::string_view(candidate).substr(verified.value.start,
            verified.value.end - verified.value.start) != desired) {
      output.assign(input);
      return JsonEditResult::invalid_json;
    }
    output.swap(candidate);
    return JsonEditResult::updated;
  } catch (...) {
    // No native or output edit is published until a complete candidate exists.
    try { output.assign(input); } catch (...) { output.clear(); }
    return JsonEditResult::invalid_json;
  }
}

template <typename Editor>
static FileUpdateResult UpdatePreferencesFile(
    const std::filesystem::path &path, const Editor &edit,
    std::wstring *errorDetails) noexcept {
  if (errorDetails) errorDetails->clear();
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
    const JsonEditResult editResult = edit(input, output);
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

    inputHandle.close();
    const bool published = config_persistence::MutateRawFileAtomically(
        path, [&](const std::filesystem::path &stagedPath, std::wstring &) {
          ScopedHandle stage(CreateFileW(stagedPath.c_str(), GENERIC_READ | GENERIC_WRITE,
              0, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
          LARGE_INTEGER size{};
          if (stage.get() == INVALID_HANDLE_VALUE ||
              !GetFileSizeEx(stage.get(), &size) || size.QuadPart != static_cast<LONGLONG>(input.size()))
            return false;
          std::string stagedInput(input.size(), '\0');
          if (!ReadAll(stage.get(), stagedInput) || stagedInput != input)
            return false;
          LARGE_INTEGER start{};
          return SetFilePointerEx(stage.get(), start, nullptr, FILE_BEGIN) &&
                 SetEndOfFile(stage.get()) && WriteAll(stage.get(), output);
        }, kMaximumPreferencesJsonBytes, false, errorDetails);
    if (!published)
      return FileUpdateResult::io_error;
    return FileUpdateResult::updated;
  } catch (...) {
    return FileUpdateResult::io_error;
  }
}

FileUpdateResult UpdateStartupPreferencesFile(
    const std::filesystem::path &path, bool restoreTabs,
    bool ensureSessionPreference, std::wstring *errorDetails) noexcept {
  return UpdatePreferencesFile(path, [&](std::string_view input, std::string &output) {
    return EditStartupPreferencesJson(input, restoreTabs,
                                       ensureSessionPreference, output);
  }, errorDetails);
}

FileUpdateResult UpdateTabLayoutPreferencesFile(
    const std::filesystem::path &path, TabLayoutBrowser browser, bool vertical,
    std::wstring *errorDetails) noexcept {
  return UpdatePreferencesFile(path, [&](std::string_view input, std::string &output) {
    return EditTabLayoutPreferencesJson(input, browser, vertical, output);
  }, errorDetails);
}

} // namespace browser_preferences
