#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace cleanup_result {

inline std::vector<std::wstring> Paginate(std::wstring_view text,
                                          size_t maximumPageCharacters,
                                          size_t maximumPageLines) {
  std::vector<std::wstring> pages;
  if (maximumPageCharacters < 2 || maximumPageLines == 0)
    return pages;
  if (text.empty()) {
    pages.emplace_back();
    return pages;
  }

  size_t offset = 0;
  while (offset < text.size()) {
    const size_t hardEnd =
        offset + (std::min)(maximumPageCharacters, text.size() - offset);
    size_t end = hardEnd;
    size_t lineCount = 0;
    bool lineLimited = false;
    for (size_t index = offset; index < hardEnd; ++index) {
      if (text[index] == L'\n' && ++lineCount == maximumPageLines) {
        end = index + 1;
        lineLimited = true;
        break;
      }
    }
    if (!lineLimited && hardEnd < text.size()) {
      const size_t newline = text.rfind(L'\n', hardEnd - 1);
      if (newline != std::wstring_view::npos && newline >= offset)
        end = newline + 1;
    }

    // Keep Windows newlines and UTF-16 surrogate pairs on the same page.
    if (end < text.size() && end > offset && text[end - 1] == L'\r' &&
        text[end] == L'\n') {
      --end;
    }
    if (end < text.size() && end > offset &&
        text[end - 1] >= 0xD800 && text[end - 1] <= 0xDBFF &&
        text[end] >= 0xDC00 && text[end] <= 0xDFFF) {
      --end;
    }
    if (end == offset)
      end = (std::min)(offset + maximumPageCharacters, text.size());
    pages.emplace_back(text.substr(offset, end - offset));
    offset = end;
  }
  return pages;
}

} // namespace cleanup_result
