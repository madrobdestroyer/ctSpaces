#pragma once
#include <string_view>
#include <optional>
#include <cwctype>
#include <limits>
namespace notes_text_tools {
struct Statistics {size_t words=0,characters=0,lines=1;};
inline Statistics Count(std::wstring_view text) {
  Statistics result;bool inWord=false;
  for(size_t i=0;i<text.size();++i) {
    const wchar_t ch=text[i];
    // RichEdit exposes Shift+Enter and RTF \\line as U+000B. Keep native
    // character offsets while counting these explicit breaks as logical lines.
    const bool newline=ch==L'\r'||ch==L'\n'||ch==L'\v';
    const bool space=newline||iswspace(ch)!=0;
    if(!space&&!inWord)++result.words;
    inWord=!space;
    ++result.characters;
    if(newline){++result.lines;if(ch==L'\r'&&i+1<text.size()&&text[i+1]==L'\n')++i;}
    else if(ch>=0xD800&&ch<=0xDBFF&&i+1<text.size()&&text[i+1]>=0xDC00&&text[i+1]<=0xDFFF)++i;
  }
  return result;
}
inline std::optional<size_t> LineStart(std::wstring_view text,size_t requested) {
  if(!requested)return std::nullopt;
  if(requested==1)return 0;
  size_t line=1;
  for(size_t i=0;i<text.size();++i)if(text[i]==L'\r'||text[i]==L'\n'||text[i]==L'\v') {
    if(text[i]==L'\r'&&i+1<text.size()&&text[i+1]==L'\n')++i;
    if(++line==requested)return i+1;
  }
  return std::nullopt;
}
inline std::optional<size_t> ParseLine(std::wstring_view value) {
  if(value.empty()||value.size()>10)return std::nullopt;
  size_t line=0;
  for(wchar_t ch:value){if(ch<L'0'||ch>L'9')return std::nullopt;const auto digit=static_cast<size_t>(ch-L'0');if(line>((std::numeric_limits<size_t>::max)()-digit)/10)return std::nullopt;line=line*10+digit;}
  return line?std::optional<size_t>(line):std::nullopt;
}
}
